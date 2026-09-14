// ============================================================================
//  server_sync.cpp  -  SQLサーバ連携 (端末→サーバ PUSH)  詳細は server_sync.h
// ----------------------------------------------------------------------------
//  ・HTTP(S) は HTTPClient + (https は WiFiClientSecure.setInsecure())。
//    ※ setInsecure = サーバ証明書を検証しない(簡易)。ロリポップのSSL証明書を
//      検証したい場合は CA を埋め込んで client.setCACert(...) に差し替え可能。
//  ・送信は loopTask から分足周期(既定60s)で1点。TLSハンドシェイクで数秒かかり得るが
//    WDT(30s)内。Web(httpd)は別タスクなので UI 応答はブロックしない。
// ============================================================================
#include "server_sync.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace srv {

static WlbSettings* g_s = nullptr;
static String       g_msg = "未送信";

void begin(WlbSettings* s) { g_s = s; }
bool enabled()  { return g_s && g_s->srvEnable; }
bool hasToken() { return g_s && g_s->srvToken[0] != '\0'; }
String lastMsg(){ return g_msg; }

// WiFi STA MAC → 小文字コロン区切り。
String deviceMac() {
  uint8_t m[6]; WiFi.macAddress(m);
  char b[18];
  snprintf(b, sizeof b, "%02x:%02x:%02x:%02x:%02x:%02x", m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(b);
}

static String jsonEscape(const String& in) {
  String o; o.reserve(in.length()+2);
  for (size_t i=0;i<in.length();i++){ char c=in[i];
    if (c=='"'||c=='\\') { o+='\\'; o+=c; }
    else if (c=='\n'||c=='\r') { /* drop */ }
    else o+=c; }
  return o;
}

// base + root を連結して完全URLに (末尾/先頭スラッシュを軽く正規化)。
static String url(const char* leaf) {
  String u = String(g_s->srvBase);
  while (u.endsWith("/")) u.remove(u.length()-1);
  String r = String(g_s->srvRoot);
  if (!r.startsWith("/")) r = "/" + r;
  while (r.endsWith("/")) r.remove(r.length()-1);
  return u + r + "/" + leaf;
}

// POST 共通。scheme により secure/plain を選択。resp に本文、戻り値=HTTPステータス(<0=失敗)。
static int doPost(const String& fullUrl, const String& bearer, const String& body, String& resp) {
  if (!WiFi.isConnected()) { resp = "no wifi"; return -1000; }
  bool secure = fullUrl.startsWith("https");
  int code;
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(9000);
  if (secure) {
    WiFiClientSecure client; client.setInsecure();      // 証明書検証なし(簡易)
    if (!http.begin(client, fullUrl)) { resp="begin"; return -1001; }
    http.addHeader("Content-Type", "application/json");
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    code = http.POST((uint8_t*)body.c_str(), body.length());
    resp = (code>0) ? http.getString() : http.errorToString(code);
    http.end();
  } else {
    WiFiClient client;
    if (!http.begin(client, fullUrl)) { resp="begin"; return -1001; }
    http.addHeader("Content-Type", "application/json");
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    code = http.POST((uint8_t*)body.c_str(), body.length());
    resp = (code>0) ? http.getString() : http.errorToString(code);
    http.end();
  }
  return code;
}

// resp から "token":"<hex>" を抽出。
static String parseToken(const String& resp) {
  int i = resp.indexOf("\"token\"");
  if (i < 0) return String();
  i = resp.indexOf('"', i + 7);          // 値開始の "
  if (i < 0) return String();
  i++;
  int e = resp.indexOf('"', i);
  if (e < 0) return String();
  return resp.substring(i, e);
}

bool enroll(String& err) {
  if (!g_s) { err="no settings"; return false; }
  if (!g_s->srvBase[0]) { err="base URL 未設定"; return false; }
  if (!g_s->srvCode[0]) { err="認証コード 未設定"; return false; }
  String mac = deviceMac();
  String body = "{\"mac\":\"" + mac + "\",\"code\":\"" + jsonEscape(g_s->srvCode)
              + "\",\"name\":\"" + jsonEscape(g_s->mdns) + "\"}";
  String resp; int code = doPost(url("enroll.py"), "", body, resp);
  if (code == 200) {
    String tok = parseToken(resp);
    if (tok.length() == 64) {
      strlcpy(g_s->srvToken, tok.c_str(), sizeof g_s->srvToken);
      settings_save(*g_s);
      g_msg = "enroll 成功"; err = ""; return true;
    }
    err = "token 解析失敗"; g_msg = err; return false;
  }
  err = "enroll 失敗 (" + String(code) + ") " + resp.substring(0, 80);
  g_msg = err; return false;
}

// 有効な小数を "{\"key\":\"K\",\"value\":V}" として配列へ追加 (NaN はスキップ)。
static void addReading(String& arr, const char* key, float v, int decimals) {
  if (isnan(v)) return;
  if (arr.length()) arr += ",";
  arr += "{\"key\":\""; arr += key; arr += "\",\"value\":"; arr += String(v, decimals); arr += "}";
}

bool push(const HistSample& ls) {
  if (!enabled() || !hasToken()) return false;
  if (!WiFi.isConnected()) { g_msg = "push: 未接続"; return false; }
  if (ls.epoch <= 1700000000UL) { g_msg = "push: 時刻未確定"; return false; }  // 絶対epochのみ

  String temp, humi, pres, ln;
  addReading(temp, "air",    wlbDec2(ls.t), 2);
  addReading(humi, "main",   wlbDec2(ls.h), 2);
  addReading(pres, "main",   wlbDecP(ls.p), 2);
  addReading(ln,   "danger", (float)ls.danger, 0);
  addReading(ln,   "L",      (float)ls.lTotal, 0);   // 累計 落雷数
  addReading(ln,   "D",      (float)ls.dTotal, 0);   // 累計 妨害波数

  String body = "{\"mac\":\"" + deviceMac() + "\",\"ts\":" + String(ls.epoch);
  if (temp.length()) body += ",\"temp\":["      + temp + "]";
  if (humi.length()) body += ",\"humidity\":["  + humi + "]";
  if (pres.length()) body += ",\"pressure\":["  + pres + "]";
  if (ln.length())   body += ",\"lightning\":[" + ln   + "]";
  body += "}";

  String resp; int code = doPost(url("ingest.py"), g_s->srvToken, body, resp);
  if (code == 200) { g_msg = "push OK " + resp.substring(0, 40); return true; }
  if (code == 401 || code == 403) {
    // トークン失効/端末無効化。再enrollを促すため token をクリア。
    g_s->srvToken[0] = '\0'; settings_save(*g_s);
    g_msg = "push 認証エラー(" + String(code) + ") 再登録要"; return false;
  }
  g_msg = "push 失敗 (" + String(code) + ") " + resp.substring(0, 60);
  return false;
}

}  // namespace srv
