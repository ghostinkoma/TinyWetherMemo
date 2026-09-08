// ============================================================================
//  settings.cpp  -  LittleFS /settings.ini (key=value) 永続化。
//  ※ AquaController の /wifi.ini 方式に準拠 (NVS でなくファイル)。確実に永続化。
// ============================================================================
#include "settings.h"
#include <LittleFS.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"
#include "esp_mac.h"

static const char* PATH = "/settings.ini";

static String sanitize(const String& in) {   // 改行/復帰除去 (key=value 注入防止)
  String o; o.reserve(in.length());
  for (size_t i=0;i<in.length();i++){ char c=in[i]; if(c=='\r'||c=='\n')continue; o+=c; }
  return o;
}

// ---- WiFiパスワード at-rest 暗号化 (ESP32 HW AES-256-CBC / HW SHA-256) ----
//  鍵 = SHA-256(WiFi STA MAC + アプリ固定salt) = デバイス固有。IVはHW RNGで毎回生成。
//  ※フラッシュ暗号化(eFuse)なしでは鍵はMACから導出可能=難読化グレード。平文保存よりは堅牢。
static const char* KEY_SALT = "WetherLoggerBox-settings-key-v1";
static String toHex(const uint8_t* p, size_t n){ static const char* H="0123456789abcdef";
  String s; s.reserve(n*2); for(size_t i=0;i<n;i++){s+=H[p[i]>>4];s+=H[p[i]&0xF];} return s; }
static bool fromHex(const String& s, uint8_t* out, size_t n){ if(s.length()!=n*2) return false;
  auto hx=[](char c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10; return -1; };
  for(size_t i=0;i<n;i++){ int hi=hx(s[i*2]),lo=hx(s[i*2+1]); if(hi<0||lo<0)return false; out[i]=(uint8_t)((hi<<4)|lo);} return true; }
static void deriveKey(uint8_t key[32]){
  uint8_t mac[6]={0}; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  size_t sl=strlen(KEY_SALT); uint8_t* buf=(uint8_t*)malloc(6+sl);
  if(!buf){ memset(key,0,32); return; }
  memcpy(buf,mac,6); memcpy(buf+6,KEY_SALT,sl);
  mbedtls_sha256(buf,6+sl,key,0); free(buf);
}
static String aesEnc(const String& plain){
  uint8_t key[32]; deriveKey(key);
  uint8_t iv[16], iv0[16]; for(int i=0;i<16;i++) iv[i]=(uint8_t)(esp_random()&0xFF); memcpy(iv0,iv,16);
  size_t len=plain.length(); size_t pad=16-(len%16); size_t clen=len+pad; // PKCS7
  uint8_t* in=(uint8_t*)malloc(clen); uint8_t* out=(uint8_t*)malloc(clen);
  if(!in||!out){ free(in);free(out); return String(); }
  memcpy(in,plain.c_str(),len); for(size_t i=len;i<clen;i++) in[i]=(uint8_t)pad;
  mbedtls_aes_context a; mbedtls_aes_init(&a); mbedtls_aes_setkey_enc(&a,key,256);
  mbedtls_aes_crypt_cbc(&a,MBEDTLS_AES_ENCRYPT,clen,iv,in,out); mbedtls_aes_free(&a);
  String s=toHex(iv0,16)+toHex(out,clen); free(in);free(out); return s;
}
static String aesDec(const String& hex){
  if(hex.length()<32 || (hex.length()%2)) return String();
  size_t clen=hex.length()/2-16; if(clen==0||clen%16) return String();
  uint8_t key[32]; deriveKey(key); uint8_t iv[16];
  if(!fromHex(hex.substring(0,32),iv,16)) return String();
  uint8_t* in=(uint8_t*)malloc(clen); uint8_t* out=(uint8_t*)malloc(clen);
  if(!in||!out){ free(in);free(out); return String(); }
  if(!fromHex(hex.substring(32),in,clen)){ free(in);free(out); return String(); }
  mbedtls_aes_context a; mbedtls_aes_init(&a); mbedtls_aes_setkey_dec(&a,key,256);
  mbedtls_aes_crypt_cbc(&a,MBEDTLS_AES_DECRYPT,clen,iv,in,out); mbedtls_aes_free(&a);
  size_t pad=out[clen-1]; if(pad<1||pad>16){ free(in);free(out); return String(); }
  String s; s.reserve(clen-pad); for(size_t i=0;i<clen-pad;i++) s+=(char)out[i];
  free(in);free(out); return s;
}

void settings_load(WlbSettings& s) {
  WlbSettings d; s = d;                       // 既定
  LittleFS.begin(true);                       // 冪等 (datalog と共有)
  File f = LittleFS.open(PATH, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    int eq = line.indexOf('='); if (eq < 1) continue;
    String k = line.substring(0, eq); k.trim();
    String v = line.substring(eq + 1); v.trim();
    if      (k=="ssid")   strlcpy(s.ssid, v.c_str(), sizeof s.ssid);
    else if (k=="pass")   strlcpy(s.pass, v.c_str(), sizeof s.pass);            // 旧: 平文(後方互換)
    else if (k=="passenc"){ String p=aesDec(v); if(p.length()) strlcpy(s.pass, p.c_str(), sizeof s.pass); } // AES復号
    else if (k=="mdns")   strlcpy(s.mdns, v.c_str(), sizeof s.mdns);
    else if (k=="staMode")s.staMode      = (uint8_t)v.toInt();
    else if (k=="offT")   s.offT         = v.toFloat();
    else if (k=="offH")   s.offH         = v.toFloat();
    else if (k=="period") s.samplePeriodS= (uint16_t)v.toInt();
    else if (k=="avgN")   s.avgN         = (uint8_t)v.toInt();
    else if (k=="drop")   s.dropMinMax   = (uint8_t)v.toInt();
    else if (k=="asIn")   s.asIndoor     = (uint8_t)v.toInt();
    else if (k=="asNf")   s.asNoiseFloor = (uint8_t)v.toInt();
    else if (k=="asWd")   s.asWatchdog   = (uint8_t)v.toInt();
    else if (k=="asSr")   s.asSrej       = (uint8_t)v.toInt();
    else if (k=="asMn")   s.asMinNum     = (uint8_t)v.toInt();
    else if (k=="asMk")   s.asMaskDist   = (uint8_t)v.toInt();
  }
  f.close();
  if (s.samplePeriodS < 1) s.samplePeriodS = 1;
  if (s.mdns[0] == '\0') strlcpy(s.mdns, d.mdns, sizeof s.mdns);
}

void settings_save(const WlbSettings& s) {
  LittleFS.begin(true);
  File f = LittleFS.open(PATH, "w");
  if (!f) return;
  f.printf("ssid=%s\n",   sanitize(s.ssid).c_str());
  f.printf("passenc=%s\n",aesEnc(sanitize(s.pass)).c_str());   // AES-256暗号化して保存(平文非保持)
  f.printf("mdns=%s\n",   sanitize(s.mdns).c_str());
  f.printf("staMode=%u\n",s.staMode);
  f.printf("offT=%.3f\n", s.offT);
  f.printf("offH=%.3f\n", s.offH);
  f.printf("period=%u\n", s.samplePeriodS);
  f.printf("avgN=%u\n",   s.avgN);
  f.printf("drop=%u\n",   s.dropMinMax);
  f.printf("asIn=%u\n",   s.asIndoor);
  f.printf("asNf=%u\n",   s.asNoiseFloor);
  f.printf("asWd=%u\n",   s.asWatchdog);
  f.printf("asSr=%u\n",   s.asSrej);
  f.printf("asMn=%u\n",   s.asMinNum);
  f.printf("asMk=%u\n",   s.asMaskDist);
  f.close();
}
