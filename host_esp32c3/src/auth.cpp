// ============================================================================
//  auth.cpp  -  詳細は auth.h
// ============================================================================
#include "auth.h"
#include "config.h"
#include <LittleFS.h>
#include "mbedtls/sha256.h"
#include "esp_random.h"

namespace wauth {

static const char* PATH = "/auth.ini";
static String s_name, s_salt, s_hash;

struct Sess { bool used = false; String tok; uint32_t exp = 0; };
static const int MAXS = 4;
static Sess s_sess[MAXS];

// ---- 小道具 (HW SHA / HW RNG) ----
static String toHex(const uint8_t* p, size_t n) {
  static const char* H = "0123456789abcdef";
  String s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += H[p[i] >> 4]; s += H[p[i] & 0xF]; }
  return s;
}
static String sha256hex(const String& in) {            // ESP32 HW SHA-256
  uint8_t o[32];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), o, 0);
  return toHex(o, 32);
}
static String randHex(int nb) {                        // ESP32 HW RNG
  String s; s.reserve(nb * 2);
  for (int i = 0; i < nb; i++) { uint8_t b = (uint8_t)(esp_random() & 0xFF); s += toHex(&b, 1); }
  return s;
}
static String makeHash(const String& salt, const String& pass) { return sha256hex(salt + "|" + pass); }

static bool save() {
  File f = LittleFS.open(PATH, "w"); if (!f) return false;
  f.printf("name=%s\n", s_name.c_str());
  f.printf("salt=%s\n", s_salt.c_str());
  f.printf("hash=%s\n", s_hash.c_str());
  f.close(); return true;
}
static void seed() {
  s_name = WLB_AUTH_DEFAULT_USER;
  s_salt = randHex(8);
  s_hash = makeHash(s_salt, WLB_AUTH_DEFAULT_PASS);
  save();
  Serial.printf("[auth] seeded default user \"%s\" (パスワード変更推奨)\n", s_name.c_str());
}

void begin() {
  for (int i = 0; i < MAXS; i++) s_sess[i] = Sess();
  File f = LittleFS.open(PATH, "r");
  if (!f) { seed(); return; }
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    int eq = line.indexOf('='); if (eq < 0) continue;
    String k = line.substring(0, eq), v = line.substring(eq + 1);
    if (k == "name") s_name = v; else if (k == "salt") s_salt = v; else if (k == "hash") s_hash = v;
  }
  f.close();
  if (s_name.length() == 0 || s_hash.length() == 0) { seed(); return; }
  Serial.printf("[auth] loaded user \"%s\"\n", s_name.c_str());
}

bool check(const String& u, const String& p) {
  return u == s_name && s_hash == makeHash(s_salt, p);
}

static int slot() {
  uint32_t now = millis();
  for (int i = 0; i < MAXS; i++) {
    if (!s_sess[i].used) return i;
    if ((int32_t)(s_sess[i].exp - now) <= 0) return i;   // 期限切れを再利用
  }
  return 0;                                              // 満杯 → 先頭を追い出す
}

String issue(const String& u) {
  if (u != s_name) return String();
  int s = slot();
  s_sess[s].used = true;
  s_sess[s].tok  = randHex(16);                          // 128bit
  s_sess[s].exp  = millis() + WLB_AUTH_SESSION_TTL_MS;
  return s_sess[s].tok;
}

static int findSess(const String& t) {
  if (t.length() == 0) return -1;
  uint32_t now = millis();
  for (int i = 0; i < MAXS; i++) {
    if (!s_sess[i].used) continue;
    if ((int32_t)(s_sess[i].exp - now) <= 0) { s_sess[i] = Sess(); continue; }
    if (s_sess[i].tok == t) return i;
  }
  return -1;
}

bool validate(const String& t) {
  int i = findSess(t); if (i < 0) return false;
  s_sess[i].exp = millis() + WLB_AUTH_SESSION_TTL_MS;    // スライディング延長
  return true;
}
void revoke(const String& t) { int i = findSess(t); if (i >= 0) s_sess[i] = Sess(); }
String userOf(const String& t) { int i = findSess(t); return i >= 0 ? s_name : String(); }

bool changeCredentials(const String& oldPass, const String& newUser,
                       const String& newPass, String& err) {
  if (s_hash != makeHash(s_salt, oldPass)) { err = "現在のパスワードが違います"; return false; }
  String nu = newUser.length() ? newUser : s_name;
  if (nu.length() == 0) { err = "ユーザー名が空です"; return false; }
  if ((int)newPass.length() < WLB_AUTH_MIN_PASS_LEN) { err = "パスワードが短すぎます"; return false; }
  s_name = nu; s_salt = randHex(8); s_hash = makeHash(s_salt, newPass);
  if (!save()) { err = "保存に失敗しました"; return false; }
  for (int i = 0; i < MAXS; i++) s_sess[i] = Sess();     // 全失効 → 要再ログイン
  return true;
}

}  // namespace wauth
