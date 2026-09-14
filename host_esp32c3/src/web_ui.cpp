// ============================================================================
//  web_ui.cpp  -  ダッシュボード SPA + REST API (詳細は web_ui.h / Docs/SPEC.md)
// ============================================================================
#include "web_ui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "shared_state.h"
#include "datalog.h"
#include "histfs.h"
#include "wifi_session.h"
#include "auth.h"
#include "server_sync.h"   // SQLサーバ連携 (enroll/push)
#include "web_httpd.h"     // httpd を WebServer 風に扱うシム
// 自己署名 EC P-256 証明書/鍵 (HTTPS)。実運用はデバイス固有の cert_pem.h (gitignore)
// を tools/gen_cert.ps1 で生成。未生成(clone直後)は公開サンプルへフォールバックしビルド可。
#if __has_include("cert_pem.h")
  #include "cert_pem.h"
#else
  #include "cert_pem_sample.h"
#endif
#include "web_page_gz.h"   // gzip 済み SPA (AquaController 準拠, 転送量削減で高速化)
#include "chart_min_gz.h"  // ローカル同梱 Chart.js (gzip; CDN非依存=オフライン描画)
#include <time.h>

extern WifiSession net;   // main.cpp のグローバル (NTP時刻/IP/RSSI 参照用)
extern bool g_apMode;     // main.cpp: AP 単独運用中か (AP/STA 排他表示用)
static uint32_t g_rebootAt = 0;   // WiFi保存後の再起動予約

static httpd_handle_t   g_http = nullptr;   // HTTP(80)
static httpd_handle_t   g_https = nullptr;  // HTTPS(443, TLS)
static SemaphoreHandle_t g_webMtx = nullptr;// 全ハンドラ直列化(旧WebServer相当 + 静的バッファ保護)
static bool        started = false;
static WlbSettings* g_set = nullptr;

// 設定 → io の測定設定へ反映
static void applyIoConfig() {
  IoConfig c;
  c.samplePeriodMs = (uint32_t)g_set->samplePeriodS * 1000UL;
  c.avgN           = g_set->avgN;
  c.dropMinMax     = g_set->dropMinMax;
  c.offT           = g_set->offT;
  c.offH           = g_set->offH;
  g_state.setIoConfig(c);
}

static const char* addrLabel(uint8_t a) {
  switch (a) { case 0x28: return "雷ブリッジ"; case 0x38: return "AHT20";
    case 0x76: case 0x77: return "BMP280"; case 0x44: return "SHT3x";
    case 0x3C: return "OLED"; default: return "?"; }
}

// ---- 認証 (Cookie セッション) ----
static String cookieToken(HttpCtx& server) {
  if (!server.hasHeader("Cookie")) return String();
  String c = server.header("Cookie");
  int i = c.indexOf("session=");
  if (i < 0) return String();
  i += 8; int e = c.indexOf(';', i); if (e < 0) e = c.length();
  String t = c.substring(i, e); t.trim(); return t;
}
static bool authed(HttpCtx& server) {
#if WLB_AUTH_ENABLE
  return wauth::validate(cookieToken(server));
#else
  return true;
#endif
}
// 保護ハンドラ先頭で呼ぶ。未認証なら 401 を返して false。
static bool requireAuth(HttpCtx& server) {
  if (authed(server)) return true;
  server.send(401, "application/json", "{\"err\":\"auth\",\"login\":1}");
  return false;
}

// ---- /api/now ----
static void handleNow(HttpCtx& server) {
  if (!requireAuth(server)) return;
  static SystemSnapshot s;                       // loopTaskスタック節約(大きい構造体)
  g_state.get(s);
  static HistSample h[WLB_HIST_CAP];             // 約10KB→スタックでなく静的(loopTask 8KB対策)
  uint16_t n = g_state.histCopy(h, WLB_HIST_CAP);
  // 直近30分の頻度 (lTotal/dTotal は単調)。基準=30分前(なければ最古)。
  uint16_t l30 = 0, d30 = 0;
  if (n > 0) {
    uint32_t nowSec = h[n-1].epoch;
    int base = 0;
    for (int i = 0; i < n; i++) { if (nowSec - h[i].epoch <= 1800u) { base = i; break; } }
    // カウンタ巻き戻り(雷ブリッジ再起動で total が0に戻る)時は負→uint16化けを防ぐ:
    // 巻き戻り後は「リセット以降の現在総数」を最良推定として採用
    l30 = (h[n-1].lTotal >= h[base].lTotal) ? (h[n-1].lTotal - h[base].lTotal) : h[n-1].lTotal;
    d30 = (h[n-1].dTotal >= h[base].dTotal) ? (h[n-1].dTotal - h[base].dTotal) : h[n-1].dTotal;
  }
  String j; j.reserve(1024);
  auto f=[&](bool v,float x,int d){ return v?String(x,d):String("null"); };
  j += "{";
  // ダッシュボードは小数第2位に統一(2桁目で四捨五入)。温度/湿度/気圧とも 2 桁。
  j += "\"t\":"+f(s.env.tValid,s.env.tempC,2)+",\"h\":"+f(s.env.hValid,s.env.rh,2)
     + ",\"p\":"+f(s.env.pValid,s.env.presHpa,2);
  j += ",\"danger\":"+String(s.ln.dangerPct)+",\"level\":\""+String(s.ln.levelLabel)+"\"";
  j += ",\"bridge\":"+String(s.ln.bridgeOk?1:0);
  j += ",\"L\":"+String(s.ln.lightningTotal)+",\"D\":"+String(s.ln.disturberTotal)
     + ",\"N\":"+String(s.ln.noiseTotal)+",\"near\":"+String(s.ln.nearestKm);
  j += ",\"l30\":"+String(l30)+",\"d30\":"+String(d30);
  j += ",\"scan\":[";
  for (uint8_t i=0;i<s.scanCount;i++){ if(i)j+=","; j+="["+String(s.scanList[i])+",\""+addrLabel(s.scanList[i])+"\"]"; }
  j += "]";
  { String d(s.dbg); d.replace("\\","\\\\"); d.replace("\"","\\\""); j += ",\"dbg\":\""+d+"\""; }
  bool sta = (WiFi.status()==WL_CONNECTED);
  j += ",\"sta\":{\"c\":"+String(sta?1:0)+",\"ip\":\""+WiFi.localIP().toString()
     + "\",\"rssi\":"+String(sta?WiFi.RSSI():0)+",\"mdns\":\""+String(g_set->mdns)+"\"}";
  // NTP 時刻 (epoch=UTC秒, tv=確定, local=JST epoch)。ページ1表示/チャートX軸/CSV に使用。
  j += ",\"epoch\":"+String(net.epoch())+",\"tv\":"+String(net.timeValid()?1:0)
     + ",\"tz\":"+String(WLB_TZ_OFFSET_S);
  j += ",\"ap\":{\"ip\":\""+WiFi.softAPIP().toString()+"\",\"ssid\":\""+String(WLB_AP_SSID)+"\"}";
  j += ",\"apMode\":"+String(g_apMode?1:0);   // 1=AP単独 / 0=STA
  j += ",\"calib\":{\"valid\":"+String(s.calib.valid?1:0)+",\"tuncap\":"+String(s.calib.tuncap)
     + ",\"ok\":"+String(s.calib.ok)+",\"count\":"+String(s.calib.count)+",\"target\":"+String(s.calib.target)+"}";
  j += ",\"sensors\":[";
  for (uint8_t i=0;i<s.nSamples;i++){ const auto&r=s.samples[i]; if(i)j+=",";
    j += "{\"n\":\""+String(r.name)+"\",\"t\":"+f(r.r.tValid,r.r.tempC,2)
       + ",\"h\":"+f(r.r.hValid,r.r.rh,1)+",\"p\":"+f(r.r.pValid,r.r.presHpa,1)+"}"; }
  j += "]}";
  server.send(200,"application/json; charset=utf-8",j);
}

// ---- /api/history (Phase1: RAMリング=timeスコープ) ----
static void handleHistory(HttpCtx& server) {
  if (!requireAuth(server)) return;
  static HistSample h[WLB_HIST_CAP];             // 静的(loopTaskスタック8KB対策)
  String sc = server.hasArg("scope") ? server.arg("scope") : String("time");
  uint16_t n;
  // FS には分足(period×n平均, 絶対epoch)を永続化。チャートは分解能=分足で、スコープ=期間窓。
  if      (sc=="min")  {                                            // 分足: 直近は RAM, 無ければ FS
    n = g_state.histCopyMin(h, WLB_TIER_CAP);
    if (n < 2) { uint32_t e=net.epoch(); n = histfs_query(e>2UL*86400?e-2UL*86400:0, h, WLB_TIER_CAP); }
  }
  else if (sc=="hour" || sc=="day" || sc=="week" || sc=="month") {  // FS 分足を期間窓で間引き
    uint32_t nowE = net.epoch();
    uint32_t win = (sc=="hour")? 1UL*86400 : (sc=="day")? 7UL*86400 : (sc=="week")? 30UL*86400 : 0; // 時=1日/日=7日/週=30日/月=全期間
    uint32_t since = (nowE>0 && win>0 && nowE>win)? (nowE-win) : 0;
    n = histfs_query(since, h, WLB_TIER_CAP);
    if (n < 2) n = g_state.histCopyMin(h, WLB_TIER_CAP);            // FS未蓄積時は RAM 分足
    if (n < 2) n = g_state.histCopy(h, WLB_HIST_CAP);               // それも無ければライブ
  }
  else                 n = g_state.histCopy(h, WLB_HIST_CAP);       // ライブ(5秒)
  uint32_t nowSec = (n>0)? h[n-1].epoch : 0;
  const uint16_t MAX_OUT = 120;                  // 代表値間引き(AquaController準拠): 転送量/描画負荷を削減
  uint16_t stride = (n > MAX_OUT) ? (uint16_t)((n + MAX_OUT - 1)/MAX_OUT) : 1;
  String j; j.reserve(3072);
  auto arr=[&](const char* key, int which){
    j += "\""; j+=key; j+="\":[";
    bool first=true;
    for (uint16_t i=0;i<n;i+=stride){ if(!first)j+=","; first=false;
      float v = which==0? (float)(nowSec-h[i].epoch)
              : which==1? wlbDec2(h[i].t) : which==2? wlbDec2(h[i].h) : which==3? wlbDecP(h[i].p) : (float)h[i].danger;
      if (isnan(v)) j+="null"; else j+=String(v, which==0?0:(which==3?0:2));
    }
    j += "]";
  };
  j += "{"; arr("age",0); j+=","; arr("temp",1); j+=","; arr("hum",2); j+=","; arr("pres",3); j+=","; arr("danger",4); j += "}";
  server.send(200,"application/json; charset=utf-8",j);
}

// ---- /api/settings ----
static void handleSettings(HttpCtx& server) {
  if (!requireAuth(server)) return;
  const WlbSettings& s=*g_set;
  String j="{";
  j += "\"ssid\":\""+String(s.ssid)+"\",\"mdns\":\""+String(s.mdns)+"\",\"staMode\":"+String(s.staMode);
  j += ",\"offT\":"+String(s.offT,2)+",\"offH\":"+String(s.offH,1);
  j += ",\"period\":"+String(s.samplePeriodS)+",\"avgN\":"+String(s.avgN)+",\"drop\":"+String(s.dropMinMax);
  j += ",\"asIndoor\":"+String(s.asIndoor)+",\"asNf\":"+String(s.asNoiseFloor)+",\"asWd\":"+String(s.asWatchdog);
  j += ",\"asSr\":"+String(s.asSrej)+",\"asMn\":"+String(s.asMinNum)+",\"asMk\":"+String(s.asMaskDist);
  j += ",\"logBytes\":"+String(datalog_bytes());
  j += ",\"fsTotal\":"+String(datalog_fs_total())+",\"fsUsed\":"+String(datalog_fs_used());
  // 記録可能日数の算定用(JS): ログ予算/日数上限/1行バイト/時足FS保持日数
  j += ",\"logBudget\":"+String((unsigned long)WLB_LOG_BUDGET)+",\"logMaxDays\":"+String(WLB_LOG_MAXDAYS);
  j += ",\"rowBytes\":"+String(WLB_LOG_ROWBYTES)+",\"histDays\":"+String(WLB_HISTFS_CAP/24)+"}";
  server.send(200,"application/json; charset=utf-8",j);
}

// 同期スキャン(隠しSSID含む)。AquaController 準拠: setSleep(false) 済みなら AP_STA でも可。
//  show_hidden=true で SSID 秘匿 AP も列挙。{scanning:0,aps:[{ssid,rssi}]}
static void handleWifiScan(HttpCtx& server) {
  if (!requireAuth(server)) return;
  int n = WiFi.scanNetworks(false /*async*/, true /*show_hidden*/, false /*passive*/, 300);
  String j = "{\"scanning\":0,\"n\":"+String(n)+",\"aps\":[";
  for (int i=0; i<n && i<30; i++) {
    if (i) j += ",";
    String ss = WiFi.SSID(i); ss.replace("\\","\\\\"); ss.replace("\"","\\\"");
    j += "{\"ssid\":\""+ss+"\",\"rssi\":"+String(WiFi.RSSI(i))+"}";
  }
  j += "]}";
  WiFi.scanDelete();
  server.send(200,"application/json; charset=utf-8",j);
}

static void handleWifiSet(HttpCtx& server) {
  if (!requireAuth(server)) return;
  if (server.hasArg("ssid")) strlcpy(g_set->ssid, server.arg("ssid").c_str(), sizeof g_set->ssid);
  if (server.hasArg("pass")) strlcpy(g_set->pass, server.arg("pass").c_str(), sizeof g_set->pass);
  if (server.hasArg("mdns")) strlcpy(g_set->mdns, server.arg("mdns").c_str(), sizeof g_set->mdns);
  if (server.hasArg("mode")) g_set->staMode = (uint8_t)server.arg("mode").toInt();
  settings_save(*g_set);
  g_rebootAt = millis() + 1200;   // 応答返却後に再起動 → 新SSID/mDNSでSTA接続 (AquaController準拠)
  server.send(200,"application/json","{\"ok\":1,\"reboot\":1}");
}

// ---- /api/server (サーバ連携設定 + enroll) ----
static void handleServerGet(HttpCtx& server) {
  if (!requireAuth(server)) return;
  const WlbSettings& s=*g_set;
  auto esc=[](String v){ v.replace("\\","\\\\"); v.replace("\"","\\\""); return v; };
  String j="{";
  j += "\"enable\":"+String(s.srvEnable);
  j += ",\"base\":\""+esc(String(s.srvBase))+"\"";
  j += ",\"root\":\""+esc(String(s.srvRoot))+"\"";
  j += ",\"code\":\""+esc(String(s.srvCode))+"\"";
  j += ",\"hasToken\":"+String(srv::hasToken()?1:0);
  j += ",\"mac\":\""+srv::deviceMac()+"\"";
  { String m=srv::lastMsg(); j += ",\"msg\":\""+esc(m)+"\""; }
  j += "}";
  server.send(200,"application/json; charset=utf-8",j);
}

static void handleServerSet(HttpCtx& server) {
  if (!requireAuth(server)) return;
  if (server.hasArg("enable")) g_set->srvEnable = server.arg("enable").toInt()?1:0;
  if (server.hasArg("base"))   strlcpy(g_set->srvBase, server.arg("base").c_str(), sizeof g_set->srvBase);
  if (server.hasArg("root"))   strlcpy(g_set->srvRoot, server.arg("root").c_str(), sizeof g_set->srvRoot);
  if (server.hasArg("code"))   strlcpy(g_set->srvCode, server.arg("code").c_str(), sizeof g_set->srvCode);
  // ★enroll(TLS) は httpd タスク(小スタック)では実行しない。loopTask の自動enroll に委ねる。
  //   action=enroll でトークンを消して即再登録を促す(有効かつ未token→loopが30s内に登録)。
  if (server.arg("action")=="enroll") g_set->srvToken[0] = '\0';
  settings_save(*g_set);
  server.send(200,"application/json","{\"ok\":1,\"hasToken\":"+String(srv::hasToken()?1:0)+"}");
}

// サーバ側クランプ (不正値がAS3935レジスタ/設定へ入るのを防ぐ)
static long clampL(long v, long lo, long hi){ return v<lo?lo:(v>hi?hi:v); }

static void handleCalib(HttpCtx& server) {
  if (!requireAuth(server)) return;
  String op = server.arg("op");
  long v = server.arg("val").toInt();
  bool ok = true;
  if      (op=="run")    ok = g_state.pushCmd(IOCMD_RECAL, 0);
  else if (op=="indoor"){ v=v?1:0;            g_set->asIndoor=(uint8_t)v;     ok=g_state.pushCmd(IOCMD_GAIN, v); }
  else if (op=="nf")    { v=clampL(v,0,7);    g_set->asNoiseFloor=(uint8_t)v; ok=g_state.pushCmd(IOCMD_NOISEFLOOR, v); }
  else if (op=="wdth")  { v=clampL(v,0,15);   g_set->asWatchdog=(uint8_t)v;   ok=g_state.pushCmd(IOCMD_WATCHDOG, v); }
  else if (op=="srej")  { v=clampL(v,0,15);   g_set->asSrej=(uint8_t)v;       ok=g_state.pushCmd(IOCMD_SREJ, v); }
  else if (op=="minnum"){ v=clampL(v,0,3);    g_set->asMinNum=(uint8_t)v;     ok=g_state.pushCmd(IOCMD_MINNUM, v); }
  else if (op=="mask")  { v=v?1:0;            g_set->asMaskDist=(uint8_t)v;   ok=g_state.pushCmd(IOCMD_MASKDIST, v); }
  else if (op=="clear") ok = g_state.pushCmd(IOCMD_CLEARSTATS, 0);
  else { server.send(400,"application/json","{\"err\":\"op\"}"); return; }
  settings_save(*g_set);
  server.send(200,"application/json", ok?"{\"ok\":1}":"{\"ok\":0,\"busy\":1}");
}

static void handleOffset(HttpCtx& server) {
  if (!requireAuth(server)) return;
  if (server.hasArg("t")) g_set->offT = (float)clampL((long)(server.arg("t").toFloat()*100),-2000,2000)/100.0f; // ±20℃
  if (server.hasArg("h")) g_set->offH = (float)clampL((long)(server.arg("h").toFloat()*100),-5000,5000)/100.0f; // ±50%
  settings_save(*g_set); applyIoConfig();
  server.send(200,"application/json","{\"ok\":1}");
}

static void handleLogcfg(HttpCtx& server) {
  if (!requireAuth(server)) return;
  uint16_t period = g_set->samplePeriodS; uint8_t n=g_set->avgN, drop=g_set->dropMinMax;
  if (server.hasArg("period")) period = (uint16_t)clampL(server.arg("period").toInt(), 1, 3600);   // 1s..1h
  if (server.hasArg("drop"))   drop   = server.arg("drop").toInt()?1:0;
  if (server.hasArg("n"))      n      = (uint8_t)clampL(server.arg("n").toInt(), 1, 20);            // 1..20
  if (drop && n < 4) { server.send(400,"application/json","{\"err\":\"最小最大を捨てる場合 n>=4\"}"); return; }
  g_set->samplePeriodS=period; g_set->avgN=n; g_set->dropMinMax=drop;
  settings_save(*g_set); applyIoConfig();
  server.send(200,"application/json","{\"ok\":1}");
}

static void handleCsv(HttpCtx& server) { if (!requireAuth(server)) return; datalog_stream_csv(server); }
static void handleLogClear(HttpCtx& server) { if (!requireAuth(server)) return; datalog_clear(); histfs_clear(); server.send(200,"application/json","{\"ok\":1}"); }

// ---- 認証エンドポイント (login/authは無保護、logout/passwdは要認証) ----
static void handleLogin(HttpCtx& server) {
  String u = server.arg("user"), p = server.arg("pass");
  if (!wauth::check(u, p)) { server.send(401, "application/json", "{\"ok\":0}"); return; }
  String tok = wauth::issue(u);
  server.sendHeader("Set-Cookie", "session=" + tok + "; Path=/; HttpOnly; Max-Age=86400; SameSite=Lax");
  server.send(200, "application/json", "{\"ok\":1}");
}
static void handleLogout(HttpCtx& server) {
  wauth::revoke(cookieToken(server));
  server.sendHeader("Set-Cookie", "session=; Path=/; HttpOnly; Max-Age=0; SameSite=Lax");
  server.send(200, "application/json", "{\"ok\":1}");
}
static void handleAuthStatus(HttpCtx& server) {
  bool ok = authed(server);
  String u = ok ? wauth::userOf(cookieToken(server)) : String();
  String j = "{\"enabled\":"; j += (WLB_AUTH_ENABLE ? "1" : "0");
  j += ",\"authed\":" + String(ok ? 1 : 0) + ",\"user\":\"" + u + "\"}";
  server.send(200, "application/json; charset=utf-8", j);
}
static void handlePasswd(HttpCtx& server) {
  if (!requireAuth(server)) return;
  String err;
  if (wauth::changeCredentials(server.arg("old"), server.arg("user"), server.arg("new"), err))
    server.send(200, "application/json", "{\"ok\":1}");
  else { String j = "{\"ok\":0,\"err\":\"" + err + "\"}"; server.send(200, "application/json; charset=utf-8", j); }
}
// ---- Chart.js ローカル配信 (gzip; 無認証=描画に必要) ----
static void handleChartJs(HttpCtx& server) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "max-age=604800");   // 1週間キャッシュ
  server.send_P(200, "application/javascript", (const uint8_t*)WLB_CHART_GZ, WLB_CHART_GZ_LEN);
}

// ============================ SPA (HTML/CSS/JS) =============================
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>WetherLoggerBox</title>
<script src="/chart.min.js"></script>
<style>
:root{--bg:#fff;--fg:#111;--card:#f6faf7;--line:#5fb98c;--acc:#2f9e6a;--mut:#5a6b62}
:root[data-theme=dark]{--bg:#0b1020;--fg:#eef2f8;--card:#131a2e;--line:#26406a;--acc:#5fd08a;--mut:#8fa3bf}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:15px}
header{position:sticky;top:0;display:flex;align-items:center;gap:10px;padding:10px 12px;
background:var(--card);border-bottom:2px solid var(--line);z-index:10}
header h1{font-size:16px;margin:0;flex:1}
button,select,input{font:inherit;color:var(--fg);background:var(--bg);border:1px solid var(--line);
border-radius:8px;padding:7px 10px}
.icon{background:var(--card);cursor:pointer;padding:6px 10px;font-size:18px}
nav{position:fixed;top:0;left:-260px;width:250px;height:100%;background:var(--card);
border-right:2px solid var(--line);transition:left .2s;z-index:20;padding-top:56px}
nav.open{left:0}nav a{display:block;padding:14px 18px;color:var(--fg);text-decoration:none;
border-bottom:1px solid var(--line);cursor:pointer}nav a.on{background:var(--acc);color:#fff}
.mask{position:fixed;inset:0;background:#0006;z-index:15;display:none}.mask.open{display:block}
main{padding:14px;max-width:820px;margin:0 auto}section{display:none}section.on{display:block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:10px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.big{font-size:34px;font-weight:700}.mut{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse}td,th{border:1px solid var(--line);padding:6px 8px;text-align:left;font-size:14px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
label{min-width:120px}.gauge{height:14px;border-radius:7px;background:#8883;overflow:hidden}
.gauge>i{display:block;height:100%;background:var(--acc)}
.lv0{color:#5fd08a}.lv1{color:#c9d05f}.lv2{color:#e0b04f}.lv3{color:#e07a4f}.lv4{color:#ff5b5b}
.ok{color:#5fd08a}.ng{color:#ff6b6b}.chartwrap{position:relative;height:260px}
h2{font-size:15px;margin:4px 0 8px}.note{font-size:12px;color:var(--mut)}
@media(max-width:520px){.grid{grid-template-columns:1fr}label{min-width:96px}}
.srow{margin:12px 0}.srow>label{display:flex;align-items:center;gap:6px;min-width:0;margin-bottom:4px}
.row2{display:flex;align-items:center;gap:10px}
input[type=range]{flex:1;accent-color:var(--acc);height:22px}
.sval{font-weight:700;color:var(--acc);min-width:52px;text-align:right;font-variant-numeric:tabular-nums}
.info{width:20px;height:20px;flex:0 0 20px;border-radius:50%;border:1px solid var(--acc);
 background:var(--card);color:var(--acc);font:italic 700 12px/18px serif;cursor:pointer;padding:0}
.tip{background:var(--card);border:1px solid var(--acc);border-radius:8px;padding:8px 10px;
 font-size:13px;line-height:1.5;margin:4px 0 2px;color:var(--fg)}
#login{position:fixed;inset:0;background:#0009;z-index:30;display:none;align-items:center;justify-content:center}
#login.on{display:flex}
#login .box{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:20px;width:280px;max-width:90%}
#login input{width:100%;margin:6px 0}
</style></head><body>
<header><button class=icon id=mbtn>☰</button><h1>WetherLoggerBox</h1>
<button class=icon id=obtn title=logout hidden>⎋</button>
<button class=icon id=tbtn title=theme>◐</button></header>
<div id=login><div class=box>
 <h2 data-i18n=login.title>Login</h2>
 <input id=lu data-i18n-ph=login.user placeholder=Username autocomplete=username>
 <input id=lpw type=password data-i18n-ph=login.pass placeholder=Password autocomplete=current-password>
 <div class=row><button id=lbtn style=flex:1 data-i18n=login.btn>Log in</button></div>
 <div id=lerr class=ng style=font-size:13px></div>
</div></div>
<div class=mask id=mask></div>
<nav id=nav>
<a data-p=0 class=on data-i18n=nav.home>Home</a><a data-p=1 data-i18n=nav.chart>Chart</a>
<a data-p=2 data-i18n=nav.wifi>WiFi</a><a data-p=3 data-i18n=nav.calib>Lightning calibration</a>
<a data-p=4 data-i18n=nav.offset>Temp/Humidity offset</a><a data-p=5 data-i18n=nav.data>Data / logging</a>
<a data-p=6 data-i18n=nav.settings>Settings</a><a data-p=7 data-i18n=nav.server>Server link</a>
</nav>
<main>
<section class="on" id=p0>
<div class=card style=padding:8px><span class=mut data-i18n=dash.time>Time (NTP)</span> <b id=clock>--</b>
  <span id=upd class=mut style=margin-left:auto></span></div>
<div class=grid>
 <div class=card><div class=mut data-i18n=dash.temp>Temperature</div><div class=big><span id=t>--</span>°C</div></div>
 <div class=card><div class=mut data-i18n=dash.humi>Humidity</div><div class=big><span id=h>--</span>%</div></div>
</div>
<div class=card><div class=mut data-i18n=dash.pres>Pressure</div><div class=big><span id=p>--</span> hPa</div></div>
<div class=card>
 <div class=row><b data-i18n=dash.risk>Lightning risk</b><span id=lv class=lv0 style=margin-left:auto></span></div>
 <div class=gauge><i id=gg style=width:0%></i></div>
 <div class=row><span class=big><span id=dg>0</span>%</span></div>
 <div class=mut><span data-i18n=dash.last30>Last 30 min</span>: <span data-i18n=dash.strikes>strikes</span> <b id=l30>0</b> / <span data-i18n=dash.dist>disturbers</span> <b id=d30>0</b>　<span data-i18n=dash.nearest>nearest</span> <b id=near>--</b> km</div>
 <div class=mut><span data-i18n=dash.cum>Cumulative L/D/N</span>: <span id=ldn>0/0/0</span>　bridge: <span id=br>--</span></div>
</div>
</section>
<section id=p1>
<div class=card>
 <div class=row><b data-i18n=chart.series>Time series</b>
  <span style=margin-left:auto></span>
  <button data-s=time class=on data-i18n=chart.live>Live</button><button data-s=min data-i18n=chart.min>1-min</button>
  <button data-s=hour data-i18n=chart.hour>Hourly</button><button data-s=day data-i18n=chart.day>Day</button>
  <button data-s=week data-i18n=chart.week>Week</button><button data-s=month data-i18n=chart.month>Month</button>
 </div>
 <div class=chartwrap><canvas id=chart></canvas></div>
 <div class=note id=chnote></div>
</div>
</section>
<section id=p2>
<div class=card><h2 data-i18n=wifi.g1>Group 1: Connect to (STA)</h2>
 <div class=row><label>SSID</label>
   <input id=wssid style=flex:1 data-i18n-ph=wifi.ssidph placeholder="Pick from list or type (hidden SSID ok)"></div>
 <div class=row><button id=wscan data-i18n=wifi.scan>WiFi scan</button><span id=wscanmsg class=mut></span></div>
 <ul id=wscanlist style="list-style:none;padding:0;margin:2px 0;max-height:170px;overflow:auto"></ul>
 <div class=row><label data-i18n=wifi.pass>Password</label><input id=wpass type=password style=flex:1></div>
 <div class=row><label data-i18n=wifi.mdns>mDNS name</label><input id=wmdns style=flex:1 placeholder=WetherMemo>
   <span class=mut>http://&lt;name&gt;.local/</span></div>
 <div class=row><label data-i18n=wifi.bootmode>Boot mode</label>
   <select id=wmode><option value=1 data-i18n=wifi.modesta>STA (normal)</option><option value=0 data-i18n=wifi.modeap>AP (setup)</option></select></div>
 <div class=row><button id=wsave data-i18n=wifi.save>Save & reboot</button><span id=wmsg class=mut></span></div>
</div>
<div class=card><h2 data-i18n=wifi.g2>Group 2: Current status</h2>
 <table>
 <tr><td data-i18n=wifi.mode>Mode</td><td id=g2mode>--</td></tr>
 <tr><td>STA</td><td id=g2sta>--</td></tr>
 <tr><td data-i18n=wifi.ip>Assigned IP</td><td id=g2ip>--</td></tr>
 <tr><td data-i18n=wifi.apssid>Device AP SSID</td><td id=g2ap>--</td></tr>
 <tr><td>AP IP</td><td id=g2apip>192.168.4.1</td></tr>
 </table>
 <div class=note data-i18n=wifi.note>AP and STA never run at the same time (security). STA is used normally; the setup AP (WPA2) starts only when STA cannot connect.</div>
</div>
<div class=card><h2 data-i18n=wifi.g3>Group 3: Account</h2>
 <div class=row><label data-i18n=acct.newuser>New username</label><input id=au data-i18n-ph=acct.keep placeholder="leave blank to keep"></div>
 <div class=row><label data-i18n=acct.curpass>Current password</label><input id=aop type=password></div>
 <div class=row><label data-i18n=acct.newpass>New password</label><input id=anp type=password></div>
 <div class=row><button id=absave data-i18n=acct.save>Change & save</button><span id=amsg class=mut></span></div>
 <div class=note data-i18n=acct.note>Stored salted with SHA-256. After a change all sessions are revoked and you must log in again.</div>
</div>
</section>
<section id=p3>
<div class=card><h2 data-i18n=calib.title>AS3935 calibration</h2>
 <div class=row><button id=crun data-i18n=calib.run>LCO recalibrate + save</button><span id=cmsg class=mut></span></div>
 <table>
 <tr><td data-i18n=calib.result>Result</td><td id=cres>--</td></tr>
 <tr><td>TUN_CAP</td><td id=ctun>--</td></tr>
 <tr><td data-i18n=calib.count>count / target</td><td id=ccnt>-- / 3125</td></tr>
 </table>
</div>
<div class=card><h2 data-i18n=sens.title>Sensitivity (per AE-AS3935)</h2>
 <div class=srow><label><span data-i18n=sens.place>Location AFE_GB</span> <button class=info data-t=in>i</button></label>
   <div class=row2><select id=cin style=flex:1><option value=1 data-i18n=sens.indoor>Indoor</option><option value=0 data-i18n=sens.outdoor>Outdoor</option></select></div>
   <div class=tip id=tip-in hidden data-i18n=tip.in>Analog front-end gain (AFE Gain Boost). <b>Indoor=0x12(18)</b> / <b>Outdoor=0x0E(14)</b>. Indoors the signal is weak so raise gain; outdoors lower it to avoid saturation. <b>Datasheet default = Indoor (0x12)</b>.</div>
 </div>
 <div class=srow><label><span data-i18n=sens.nf>Noise floor NF_LEV (0-7)</span> <button class=info data-t=nf>i</button></label>
   <div class=row2><input type=range id=cnf min=0 max=7 step=1><span class=sval id=cnfv>-</span></div>
   <div class=tip id=tip-nf hidden data-i18n=tip.nf>Noise-floor threshold level. <b>Higher = more noise-tolerant</b> (fewer INT_NH false triggers) but <b>less sensitive to weak signals</b>. Raise it in noisy environments. <b>Datasheet default = 2</b>.</div>
 </div>
 <div class=srow><label><span data-i18n=sens.wd>Sensitivity WDTH (0-15)</span> <button class=info data-t=wd>i</button></label>
   <div class=row2><input type=range id=cwd min=0 max=15 step=1><span class=sval id=cwdv>-</span></div>
   <div class=tip id=tip-wd hidden data-i18n=tip.wd>Watchdog threshold (signal validation). <b>Higher = more disturber-tolerant</b> but <b>less sensitive to weak lightning</b>. Lower = more sensitive but more false triggers. <b>Datasheet default = 2</b>.</div>
 </div>
 <div class=srow><label><span data-i18n=sens.sr>Spike rejection SREJ (0-15)</span> <button class=info data-t=sr>i</button></label>
   <div class=row2><input type=range id=csr min=0 max=15 step=1><span class=sval id=csrv>-</span></div>
   <div class=tip id=tip-sr hidden data-i18n=tip.sr>Spike (disturber) rejection strength. <b>Higher = more disturber-tolerant but lower detection efficiency</b>. Raise it where lighter sparks etc. cause false triggers. <b>Datasheet default = 2</b>.</div>
 </div>
 <div class=srow><label><span data-i18n=sens.mn>Min lightnings MIN_NUM_LIGH</span> <button class=info data-t=mn>i</button></label>
   <div class=row2><input type=range id=cmn min=0 max=3 step=1><span class=sval id=cmnv>-</span></div>
   <div class=tip id=tip-mn hidden data-i18n=tip.mn>The <b>minimum lightning count</b> in the last 15 min needed to raise INT_L (choose 1/5/9/16). <b>Higher = fewer false reports</b> but needs more strikes before reporting. <b>Datasheet default = 1</b>.</div>
 </div>
 <div class=srow><label><input id=cmk type=checkbox> <span data-i18n=sens.mask>Mask disturbers MASK_DIST</span> <button class=info data-t=mk>i</button></label>
   <div class=tip id=tip-mk hidden data-i18n=tip.mk>Do not raise an interrupt even when a disturber is detected. Turn ON where false triggers are frequent; disturber events will then not be reported. <b>Datasheet default = OFF (0)</b>.</div>
 </div>
 <div class=row><button id=csave data-i18n=sens.apply>Apply sensitivity</button><button id=cclear data-i18n=sens.clear>Clear stats</button>
   <span id=csmsg class=mut></span></div>
 <div class=note data-i18n=sens.note>Tap the "i" on each item for help. See the AE-AS3935 manual for details.</div>
</div>
</section>
<section id=p4>
<div class=card><h2 data-i18n=off.g1>Group 1: Temperature / humidity offset</h2>
 <div class=row><label data-i18n=off.t>Temp +°C</label><input id=oft type=number step=0.1 style=width:110px></div>
 <div class=row><label data-i18n=off.h>Humidity +%RH</label><input id=ofh type=number step=0.1 style=width:110px></div>
 <div class=row><button id=osave data-i18n=off.apply>Apply</button><span id=omsg class=mut></span></div>
 <div class=note data-i18n=off.note>Added to the composite display value.</div>
</div>
<div class=card><h2 data-i18n=off.g2>Group 2: Raw sensor values</h2>
 <div class=note data-i18n=off.g2note>Raw measured value of each sensor (<b>before</b> offset). Use as a guide for offset tuning.</div>
 <table><thead><tr><th data-i18n=off.sensor>Sensor</th><th data-i18n=dash.temp>Temperature</th><th data-i18n=dash.humi>Humidity</th><th data-i18n=dash.pres>Pressure</th></tr></thead>
 <tbody id=stbody><tr><td colspan=4 class=mut data-i18n=off.loading>Loading...</td></tr></tbody></table>
 <div class=note id=sdbg style=margin-top:6px></div>
 <div class=note id=sscan></div>
</div>
</section>
<section id=p5>
<div class=card><h2 data-i18n=data.freq>Measurement frequency</h2>
 <div class=row><label data-i18n=data.period>Measurement period (s)</label><input id=lp type=number min=1 style=width:90px></div>
 <div class=row><label><input id=ld type=checkbox> <span data-i18n=data.drop>Drop min/max</span></label></div>
 <div class=row><label data-i18n=data.n>Average samples n</label><input id=ln type=number min=1 style=width:90px>
   <span class=mut data-i18n=data.nhint>n≥4 when dropping</span></div>
 <div class=note id=lretain></div>
 <div class=row><button id=lsave data-i18n=data.apply>Apply</button><span id=lmsg class=mut></span></div>
</div>
<div class=card><h2 data-i18n=data.dl>Data download</h2>
 <div class=row><a href=/api/csv><button data-i18n=data.csv>CSV download</button></a>
   <button id=lclr data-i18n=data.clear>Clear log</button><span id=lsz class=mut></span></div>
 <div class=note data-i18n=data.note>Records measurements to LittleFS at the measurement period (trace). Rotates one generation at the cap.</div>
</div>
</section>
<section id=p6>
<div class=card><h2 data-i18n=set.title>Settings</h2>
 <div class=row><label data-i18n=set.lang>Language</label>
   <select id=lang><option value=en>English</option><option value=ja>日本語</option></select></div>
 <div class=note data-i18n=set.langnote>Choose the UI language. Saved on this device (browser).</div>
</div>
</section>
<section id=p7>
<div class=card><h2 data-i18n=srv.title>Server link (SQL)</h2>
 <div class=row><label><input id=sen type=checkbox> <span data-i18n=srv.enable>Enable server mode (else standalone)</span></label></div>
 <div class=row><label data-i18n=srv.base>Base URL</label><input id=sbase style=flex:1 placeholder="https://your-domain.example"></div>
 <div class=row><label data-i18n=srv.root>API root path</label><input id=sroot style=flex:1 placeholder="/webapi"></div>
 <div class=row><label data-i18n=srv.code>Auth code</label><input id=scode style=flex:1></div>
 <div class=row><button id=ssave data-i18n=srv.save>Save</button><button id=senroll data-i18n=srv.enroll>Connect (enroll)</button><span id=smsg class=mut></span></div>
 <table>
  <tr><td>MAC</td><td id=smac>--</td></tr>
  <tr><td data-i18n=srv.token>Token</td><td id=stok>--</td></tr>
  <tr><td data-i18n=srv.status>Status</td><td id=sstat>--</td></tr>
 </table>
 <div class=note data-i18n=srv.note>When enabled, each 1-min record (temp/humidity/pressure/lightning) is pushed to the SQL server. The token is derived from the MAC via HMAC on the server; enroll once.</div>
</div>
</section>
</main>
<script>
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
// ---- i18n (default=English, Japanese selectable in Settings) ----
let LANG='en';
try{if(localStorage.wlbLang)LANG=localStorage.wlbLang;}catch(e){}
const I18N={en:{
 'nav.home':'Home','nav.chart':'Chart','nav.wifi':'WiFi','nav.calib':'Lightning calibration',
 'nav.offset':'Temp/Humidity offset','nav.data':'Data / logging','nav.settings':'Settings','nav.server':'Server link',
 'srv.title':'Server link (SQL)','srv.enable':'Enable server mode (else standalone)','srv.base':'Base URL',
 'srv.root':'API root path','srv.code':'Auth code','srv.save':'Save','srv.enroll':'Connect (enroll)',
 'srv.token':'Token','srv.status':'Status',
 'srv.note':'When enabled, each 1-min record (temp/humidity/pressure/lightning) is pushed to the SQL server. The token is derived from the MAC via HMAC on the server; enroll once.',
 'login.title':'Login','login.user':'Username','login.pass':'Password','login.btn':'Log in',
 'dash.time':'Time (NTP)','dash.temp':'Temperature','dash.humi':'Humidity','dash.pres':'Pressure',
 'dash.risk':'Lightning risk','dash.last30':'Last 30 min','dash.strikes':'strikes','dash.dist':'disturbers',
 'dash.nearest':'nearest','dash.cum':'Cumulative L/D/N',
 'chart.series':'Time series','chart.live':'Live','chart.min':'1-min','chart.hour':'Hourly',
 'chart.day':'Day','chart.week':'Week','chart.month':'Month',
 'ser.temp':'Temp °C','ser.humi':'Humidity %','ser.pres':'Pressure hPa','ser.danger':'Risk %',
 'wifi.g1':'Group 1: Connect to (STA)','wifi.ssidph':'Pick from list or type (hidden SSID ok)',
 'wifi.scan':'WiFi scan','wifi.pass':'Password','wifi.mdns':'mDNS name','wifi.bootmode':'Boot mode',
 'wifi.modesta':'STA (normal)','wifi.modeap':'AP (setup)','wifi.save':'Save & reboot',
 'wifi.g2':'Group 2: Current status','wifi.mode':'Mode','wifi.ip':'Assigned IP','wifi.apssid':'Device AP SSID',
 'wifi.note':'AP and STA never run at the same time (security). STA is used normally; the setup AP (WPA2) starts only when STA cannot connect.',
 'wifi.g3':'Group 3: Account','acct.newuser':'New username','acct.keep':'leave blank to keep',
 'acct.curpass':'Current password','acct.newpass':'New password','acct.save':'Change & save',
 'acct.note':'Stored salted with SHA-256. After a change all sessions are revoked and you must log in again.',
 'calib.title':'AS3935 calibration','calib.run':'LCO recalibrate + save','calib.result':'Result','calib.count':'count / target',
 'sens.title':'Sensitivity (per AE-AS3935)','sens.place':'Location AFE_GB','sens.indoor':'Indoor','sens.outdoor':'Outdoor',
 'sens.nf':'Noise floor NF_LEV (0-7)','sens.wd':'Sensitivity WDTH (0-15)','sens.sr':'Spike rejection SREJ (0-15)',
 'sens.mn':'Min lightnings MIN_NUM_LIGH','sens.mask':'Mask disturbers MASK_DIST','sens.apply':'Apply sensitivity','sens.clear':'Clear stats',
 'sens.note':'Tap the "i" on each item for help. See the AE-AS3935 manual for details.',
 'tip.in':'Analog front-end gain (AFE Gain Boost). <b>Indoor=0x12(18)</b> / <b>Outdoor=0x0E(14)</b>. Indoors the signal is weak so raise gain; outdoors lower it to avoid saturation. <b>Datasheet default = Indoor (0x12)</b>.',
 'tip.nf':'Noise-floor threshold level. <b>Higher = more noise-tolerant</b> (fewer INT_NH false triggers) but <b>less sensitive to weak signals</b>. Raise it in noisy environments. <b>Datasheet default = 2</b>.',
 'tip.wd':'Watchdog threshold (signal validation). <b>Higher = more disturber-tolerant</b> but <b>less sensitive to weak lightning</b>. Lower = more sensitive but more false triggers. <b>Datasheet default = 2</b>.',
 'tip.sr':'Spike (disturber) rejection strength. <b>Higher = more disturber-tolerant but lower detection efficiency</b>. Raise it where lighter sparks etc. cause false triggers. <b>Datasheet default = 2</b>.',
 'tip.mn':'The <b>minimum lightning count</b> in the last 15 min needed to raise INT_L (choose 1/5/9/16). <b>Higher = fewer false reports</b> but needs more strikes before reporting. <b>Datasheet default = 1</b>.',
 'tip.mk':'Do not raise an interrupt even when a disturber is detected. Turn ON where false triggers are frequent; disturber events will then not be reported. <b>Datasheet default = OFF (0)</b>.',
 'off.g1':'Group 1: Temperature / humidity offset','off.t':'Temp +°C','off.h':'Humidity +%RH','off.apply':'Apply',
 'off.note':'Added to the composite display value.','off.g2':'Group 2: Raw sensor values',
 'off.g2note':'Raw measured value of each sensor (<b>before</b> offset). Use as a guide for offset tuning.',
 'off.sensor':'Sensor','off.loading':'Loading...',
 'data.freq':'Measurement frequency','data.period':'Measurement period (s)','data.drop':'Drop min/max',
 'data.n':'Average samples n','data.nhint':'n≥4 when dropping','data.apply':'Apply','data.dl':'Data download',
 'data.csv':'CSV download','data.clear':'Clear log',
 'data.note':'Records measurements to LittleFS at the measurement period (trace). Rotates one generation at the cap.',
 'set.title':'Settings','set.lang':'Language','set.langnote':'Choose the UI language. Saved on this device (browser).',
 'lvl.safe':'Safe','lvl.caution':'Caution','lvl.warning':'Warning','lvl.danger':'Danger','lvl.severe':'Severe',
 'dyn.loginerr':'Wrong username or password','dyn.unsynced':'Not synced (waiting NTP)',
 'dyn.connected':'Connected','dyn.notconn':'Not connected','dyn.apmode':'AP mode (STA off)',
 'dyn.nosensor':'no present sensor','dyn.diag':'Diag','dyn.i2cscan':'I2C scan','dyn.none':'none','dyn.updated':'Updated',
 'dyn.scanning':'Scanning...','dyn.scanfail':'Failed. Type the SSID directly.','dyn.apsfound':'found (click to select)',
 'dyn.calibrating':'Calibrating (~2s)...','dyn.exec':'Executed','dyn.busy':'Processing','dyn.fail':'Failed',
 'dyn.statclear':'Stats cleared','dyn.applied':'Applied','dyn.wifisaved':'Saved. Applies after reboot',
 'dyn.times':'times','dyn.confirmclear':'Clear the log?','dyn.pwchanged':'Changed. Please log in again.',
 'dyn.enrolling':'Enrolling...'
},ja:{
 'nav.home':'ホーム','nav.chart':'チャート','nav.wifi':'WiFi設定','nav.calib':'雷キャリブレーション',
 'nav.offset':'温湿度オフセット','nav.data':'データ/測定頻度','nav.settings':'設定','nav.server':'サーバ連携',
 'srv.title':'サーバ連携 (SQL)','srv.enable':'サーバ対応を有効化 (OFF=スタンドアロン)','srv.base':'接続文字列(ベースURL)',
 'srv.root':'WebAPI ルートパス','srv.code':'認証コード','srv.save':'保存','srv.enroll':'接続(登録)',
 'srv.token':'トークン','srv.status':'状態',
 'srv.note':'有効時、分足1点(温度/湿度/気圧/雷)をSQLサーバへ送信します。トークンはサーバ側でMACからHMAC生成。初回のみ「接続(登録)」を実行してください。',
 'login.title':'ログイン','login.user':'ユーザー','login.pass':'パスワード','login.btn':'ログイン',
 'dash.time':'時刻(NTP)','dash.temp':'温度','dash.humi':'湿度','dash.pres':'気圧',
 'dash.risk':'雷 危険度','dash.last30':'直近30分','dash.strikes':'落雷','dash.dist':'妨害波',
 'dash.nearest':'最短','dash.cum':'累計 L/D/N',
 'chart.series':'時系列','chart.live':'ライブ','chart.min':'分足','chart.hour':'時間足',
 'chart.day':'日','chart.week':'週','chart.month':'月',
 'ser.temp':'温度°C','ser.humi':'湿度%','ser.pres':'気圧hPa','ser.danger':'危険度%',
 'wifi.g1':'グループ1: 接続先(STA)','wifi.ssidph':'一覧から選択 or 直接入力(隠しSSID可)',
 'wifi.scan':'WiFiスキャン','wifi.pass':'パスワード','wifi.mdns':'mDNS名','wifi.bootmode':'起動モード',
 'wifi.modesta':'STA(通常)','wifi.modeap':'AP(設定用)','wifi.save':'保存して再起動',
 'wifi.g2':'グループ2: 現在の状態','wifi.mode':'モード','wifi.ip':'払い出しIP','wifi.apssid':'本機AP SSID',
 'wifi.note':'APとSTAは同時に動作しません(セキュリティ)。通常はSTA、STAが接続できない時のみ設定用AP(WPA2)を起動します。',
 'wifi.g3':'グループ3: アカウント','acct.newuser':'ユーザー名(新)','acct.keep':'空欄で現状維持',
 'acct.curpass':'現在のパスワード','acct.newpass':'新しいパスワード','acct.save':'変更して保存',
 'acct.note':'SHA-256でソルト付き保存。変更後は全セッション失効し再ログインが必要。',
 'calib.title':'AS3935 キャリブレーション','calib.run':'LCO再校正+保存','calib.result':'結果','calib.count':'count / target',
 'sens.title':'感度設定 (AE-AS3935 準拠)','sens.place':'設置場所 AFE_GB','sens.indoor':'室内','sens.outdoor':'室外',
 'sens.nf':'ノイズ床 NF_LEV （範囲 0–7）','sens.wd':'感度 WDTH （範囲 0–15）','sens.sr':'パルス除去 SREJ （範囲 0–15）',
 'sens.mn':'最小落雷数 MIN_NUM_LIGH','sens.mask':'妨害波をマスク(無視) MASK_DIST','sens.apply':'感度を適用','sens.clear':'統計クリア',
 'sens.note':'各項目の「i」をタップで説明。詳細は AE-AS3935 マニュアル参照。',
 'tip.in':'アナログ前段の利得(AFE Gain Boost)。<b>室内=0x12(18)</b>/<b>室外=0x0E(14)</b>。屋内は電波が弱いので利得を上げ、屋外は下げて飽和を防ぐ。<b>データシート既定=室内(0x12)</b>。',
 'tip.nf':'ノイズフロア閾値レベル。<b>大きいほどノイズに強い</b>(誤ノイズ割込INT_NHが減る)が、<b>弱い信号への感度が下がる</b>。ノイズの多い環境で上げる。<b>データシート既定=2</b>。',
 'tip.wd':'ウォッチドッグ閾値(信号検証のしきい)。<b>大きいほど妨害波に強い</b>が、<b>弱い雷の検出感度が下がる</b>。小さいほど高感度だが誤検出増。<b>データシート既定=2</b>。',
 'tip.sr':'スパイク(妨害波)除去の強さ。<b>大きいほど妨害波に強いが検出効率が低下</b>する。ライター火花等の誤検出が多い環境で上げる。<b>データシート既定=2</b>。',
 'tip.mn':'雷割込(INT_L)を上げるまでに必要な直近15分の<b>最小落雷数</b>(1/5/9/16 から選択)。<b>大きいほど誤報が減る</b>が、報告までに多くの落雷が必要。<b>データシート既定=1</b>。',
 'tip.mk':'妨害波(disturber)を検出しても割込を出さない。誤検出が多い環境でON。ONにすると妨害波イベントは通知されなくなる。<b>データシート既定=OFF(0)</b>。',
 'off.g1':'グループ1: 温度・湿度 オフセット','off.t':'温度 +°C','off.h':'湿度 +%RH','off.apply':'適用',
 'off.note':'合成表示値に加算されます。','off.g2':'グループ2: センサ実測値',
 'off.g2note':'各センサの生の測定値（オフセット適用<b>前</b>）。オフセット調整の目安に。',
 'off.sensor':'センサ','off.loading':'取得待ち...',
 'data.freq':'測定頻度','data.period':'測定周期(秒)','data.drop':'最小最大を捨てる',
 'data.n':'平均サンプル数 n','data.nhint':'捨てる場合 n≥4','data.apply':'適用','data.dl':'データ ダウンロード',
 'data.csv':'CSV ダウンロード','data.clear':'ログ消去',
 'data.note':'実測を LittleFS に測定周期で記録(トレース)。上限で1世代ローテート。',
 'set.title':'設定','set.lang':'言語','set.langnote':'UI言語を選択。この端末(ブラウザ)に保存されます。',
 'lvl.safe':'安全','lvl.caution':'注意','lvl.warning':'警戒','lvl.danger':'危険','lvl.severe':'厳重警戒',
 'dyn.loginerr':'ユーザー名またはパスワードが違います','dyn.unsynced':'未同期(NTP待ち)',
 'dyn.connected':'接続','dyn.notconn':'未接続','dyn.apmode':'APモード(STA停止)',
 'dyn.nosensor':'present なセンサ無し','dyn.diag':'診断','dyn.i2cscan':'I2Cスキャン','dyn.none':'なし','dyn.updated':'更新',
 'dyn.scanning':'スキャン中...','dyn.scanfail':'失敗。SSIDを直接入力してください','dyn.apsfound':'件 (クリックで選択)',
 'dyn.calibrating':'校正中(~2s)...','dyn.exec':'実行','dyn.busy':'処理中','dyn.fail':'失敗',
 'dyn.statclear':'統計クリア','dyn.applied':'適用','dyn.wifisaved':'保存。再起動で反映されます',
 'dyn.times':'回','dyn.confirmclear':'ログを消去しますか?','dyn.pwchanged':'変更しました。再ログインしてください',
 'dyn.enrolling':'登録中...'
}};
function t(k){const d=I18N[LANG]||I18N.en;return d[k]!=null?d[k]:(I18N.en[k]!=null?I18N.en[k]:k);}
function applyLang(l){LANG=(I18N[l]?l:'en');try{localStorage.wlbLang=LANG}catch(e){}
 document.documentElement.lang=LANG;
 $$('[data-i18n]').forEach(e=>{e.innerHTML=t(e.dataset.i18n);});
 $$('[data-i18n-ph]').forEach(e=>{e.placeholder=t(e.dataset.i18nPh);});
 const ls=$('#lang');if(ls)ls.value=LANG;
 try{updRetain();}catch(e){}}
// theme
const root=document.documentElement;
try{if(localStorage.wlbTheme)root.dataset.theme=localStorage.wlbTheme;}catch(e){}
$('#tbtn').onclick=()=>{const d=root.dataset.theme==='dark'?'light':'dark';root.dataset.theme=d;try{localStorage.wlbTheme=d}catch(e){};};
// menu
const nav=$('#nav'),mask=$('#mask');
const openM=v=>{nav.classList.toggle('open',v);mask.classList.toggle('open',v)};
$('#mbtn').onclick=()=>openM(!nav.classList.contains('open'));mask.onclick=()=>openM(false);
let page=0, scope='time', chart, curEpoch=0, curTv=0, pollMs=5000, pollTimer=null, pollGen=0;
// 自己スケジュール型: 前回の tick 完了を待ってから次を予約 → 単一スレッドWebServerへの
// リクエスト重複/滞留を根絶(setInterval だと重なって PENDING が積み上がる)。
function startPoll(){ const g=++pollGen;
  const run=async()=>{ if(g!==pollGen)return; try{await tick();}catch(e){}
    if(g!==pollGen)return; pollTimer=setTimeout(run, pollMs); };
  run(); }
$$('#nav a').forEach(a=>a.onclick=()=>{page=+a.dataset.p;
 $$('#nav a').forEach(x=>x.classList.toggle('on',x===a));
 $$('main section').forEach((s,i)=>s.classList.toggle('on',i===page));
 openM(false); if(page===1)drawChart(); if(page===7)loadServer();});
// API helpers (401でログイン表示)
const gj=async u=>{const r=await fetch(u); if(r.status===401){showLogin();throw new Error('auth');} return r.json();};
const post=async(u,o)=>{const r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:new URLSearchParams(o)}); if(r.status===401){showLogin();throw new Error('auth');} return r.json();};
// ---- 認証 ----
function showLogin(){$('#login').classList.add('on');}
function hideLogin(){$('#login').classList.remove('on');}
async function checkAuth(){try{const a=await gj('/api/auth');
 $('#obtn').hidden=!(a.enabled&&a.authed);
 if(a.enabled&&!a.authed){showLogin();return false;} hideLogin();return true;}catch(e){return false;}}
$('#lbtn').onclick=async()=>{$('#lerr').textContent='';
 const r=await post('/api/login',{user:$('#lu').value,pass:$('#lpw').value}).catch(()=>({ok:0}));
 if(r&&r.ok){$('#lpw').value='';hideLogin();$('#obtn').hidden=false;loadSettings();}else{$('#lerr').textContent=t('dyn.loginerr');}};
$('#lpw').addEventListener('keydown',e=>{if(e.key==='Enter')$('#lbtn').click();});
$('#obtn').onclick=async()=>{await post('/api/logout',{}).catch(()=>{});showLogin();$('#obtn').hidden=true;};
// ---- P1 live ----
async function tick(){try{const d=await gj('/api/now');
 curEpoch=d.epoch||0;curTv=d.tv||0;
 $('#clock').textContent=d.tv?new Date((d.epoch)*1000).toLocaleString(LANG==='ja'?'ja-JP':'en-US'):t('dyn.unsynced');
 const fx=v=>(v==null?'--':Number(v).toFixed(2));   // 小数第2位に統一(2桁目で四捨五入)
 $('#t').textContent=fx(d.t);$('#h').textContent=fx(d.h);$('#p').textContent=fx(d.p);
 $('#dg').textContent=d.danger;$('#gg').style.width=d.danger+'%';
 const li=d.danger<5?0:d.danger<25?1:d.danger<50?2:d.danger<75?3:4;   // 危険度→レベル(JSで算定=言語非依存)
 const lv=$('#lv');lv.textContent=t(['lvl.safe','lvl.caution','lvl.warning','lvl.danger','lvl.severe'][li]);lv.className='lv'+li;
 $('#l30').textContent=d.l30;$('#d30').textContent=d.d30;
 $('#near').textContent=d.near<0?'--':(d.near===63?'>40':d.near);
 $('#ldn').textContent=d.L+'/'+d.D+'/'+d.N;
 $('#br').innerHTML=d.bridge?'<span class=ok>ok</span>':'<span class=ng>NG</span>';
 // P2 status (AP/STA 排他: モード表示)
 $('#g2mode').textContent=d.apMode?t('dyn.apmode'):'STA';
 $('#g2sta').innerHTML=d.sta.c?('<span class=ok>'+t('dyn.connected')+'</span> '+d.sta.rssi+'dBm'):'<span class=ng>'+t('dyn.notconn')+'</span>';
 $('#g2ip').textContent=d.sta.ip;$('#g2apip').textContent=d.ap.ip;$('#g2ap').textContent=d.ap.ssid;
 // P4 calib result
 if(d.calib.valid){$('#cres').textContent=d.calib.ok?'OK':'NG';
  $('#ctun').textContent=d.calib.tuncap;$('#ccnt').textContent=d.calib.count+' / '+d.calib.target;}
 // センサ実測テーブル
 const tb=$('#stbody');
 if(d.sensors&&d.sensors.length){tb.innerHTML=d.sensors.map(s=>'<tr><td>'+s.n+'</td><td>'
  +(s.t??'--')+'</td><td>'+(s.h??'--')+'</td><td>'+(s.p??'--')+'</td></tr>').join('');}
 else{tb.innerHTML='<tr><td colspan=4 class=ng>'+t('dyn.nosensor')+'</td></tr>';}
 $('#sdbg').textContent=t('dyn.diag')+': '+(d.dbg||'');
 $('#sscan').textContent=t('dyn.i2cscan')+': '+(d.scan&&d.scan.length?d.scan.map(x=>'0x'+x[0].toString(16)+'('+x[1]+')').join(' '):t('dyn.none'));
 $('#upd').textContent=t('dyn.updated')+' '+new Date().toLocaleTimeString();
 // 更新周期に合わせてページ2チャートも再描画 (await で重複回避)
 if(page===1)await drawChart();
}catch(e){}}
startPoll();
// ---- P2 chart ----
$$('#p1 button[data-s]').forEach(b=>b.onclick=()=>{scope=b.dataset.s;
 $$('#p1 button[data-s]').forEach(x=>x.classList.toggle('on',x===b));drawChart();});
// データ系列メタ: [履歴JSONキー, 色, 軸, i18nラベルキー]
const DSMETA=[['temp','#e07a4f','y','ser.temp'],['hum','#5fb0d0','y','ser.humi'],
  ['pres','#9f7fe0','y2','ser.pres'],['danger','#ff5b5b','y','ser.danger']];
async function drawChart(){let d;try{d=await gj('/api/history?scope='+scope);}catch(e){return;}
 const labels=d.age.map(a=> curTv? new Date((curEpoch-a)*1000).toLocaleTimeString(LANG==='ja'?'ja-JP':'en-US') : ('-'+a+'s'));
 if(!chart){
   const ds=DSMETA.map(m=>({label:t(m[3]),data:d[m[0]],borderColor:m[1],yAxisID:m[2]}));
   chart=new Chart($('#chart'),{type:'line',data:{labels,datasets:ds},options:{animation:false,responsive:true,
    maintainAspectRatio:false,elements:{point:{radius:0}},
    scales:{y:{position:'left'},y2:{position:'right',grid:{drawOnChartArea:false}}}}});
 } else {
   // ★datasets 配列は差し替えず data のみ更新 → 凡例クリックの表示/非表示(hidden)が
   //   Ajax 更新をまたいで維持される (以前は chart.data 差替で選択が解除され全系列描画されていた)。
   chart.data.labels=labels;
   DSMETA.forEach((m,i)=>{chart.data.datasets[i].data=d[m[0]];chart.data.datasets[i].label=t(m[3]);});
   chart.update('none');
 }}
// ---- P3 wifi ----
let curCfg={};
// 記録可能日数を算定して表示。FS には period×n(=分足)毎に1レコード保存するので、
// レコード間隔 = 周期×n。CSV日量 = (86400/間隔)×1行バイト。
function logSizeText(s){const kb=((s.logBytes||0)/1024|0),used=((s.fsUsed||0)/1024|0),tot=((s.fsTotal||0)/1048576).toFixed(2);
 return LANG==='ja'?('ログ '+kb+' KB / FS '+used+' KB使用 / 総 '+tot+' MB')
                   :('Log '+kb+' KB / FS '+used+' KB used / total '+tot+' MB');}
function updRetain(){const c=curCfg; if(!c.logBudget)return;
 const period=Math.max(1,parseInt($('#lp').value)||c.period||5);
 const n=Math.max(1,parseInt($('#ln').value)||c.avgN||12);
 const iv=period*n;                                                 // FSレコード間隔[秒](=分足)
 const perDay=Math.max(1,Math.round(86400/iv*(c.rowBytes||31)));    // CSV bytes/日
 const days=Math.min(c.logMaxDays||90, Math.floor(c.logBudget/perDay));
 const kb=(perDay/1024).toFixed(1), mb=(c.logBudget/1e6).toFixed(1), cap=(c.logMaxDays||90);
 const lr=$('#lretain'); if(lr) lr.textContent = LANG==='ja'
   ? ('FS保存間隔: '+iv+'秒ごと(周期'+period+'s×n'+n+') / 記録可能日数: 約 '+days+' 日 ('+kb+'KB/日, 上限'+cap+'日・'+mb+'MB。超過分は最古1日から再帰削除)')
   : ('FS interval: every '+iv+'s (period '+period+'s × n'+n+') / Retention: ~'+days+' days ('+kb+'KB/day, cap '+cap+' days · '+mb+'MB; oldest 1 day recycled on overflow)');
 const cn=$('#chnote'); if(cn) cn.textContent = LANG==='ja'
   ? ('ライブ足=5秒(RAM)。分足='+iv+'秒平均をFSに永続化(最小最大除外)。日〜月は分足を期間で集約。記録可能 約'+days+'日。')
   : ('Live = 5 s (RAM). 1-min = '+iv+'s average persisted to FS (min/max dropped). Day-Month aggregate the 1-min series. Retention ~'+days+' days.');}
async function loadSettings(){const s=await gj('/api/settings'); curCfg=s;
 $('#wmdns').value=s.mdns;$('#wmode').value=s.staMode;
 $('#oft').value=s.offT;$('#ofh').value=s.offH;
 $('#lp').value=s.period;$('#ln').value=s.avgN;$('#ld').checked=!!s.drop;
 $('#cin').value=s.asIndoor;$('#cnf').value=s.asNf;$('#cwd').value=s.asWd;$('#csr').value=s.asSr;$('#cmn').value=s.asMn;
 updSliders(); updRetain();
 $('#cmk').checked=!!s.asMk;
 $('#lsz').textContent=logSizeText(s);
 $('#wssid').value=s.ssid||'';
 pollMs=Math.max(1000,(s.period||5)*1000);startPoll();}   // Ajax更新=測定周期に同期
['lp','ln'].forEach(id=>$('#'+id).addEventListener('input',updRetain)); // 周期/nで日数を即再計算
async function realScan(){const ul=$('#wscanlist');ul.innerHTML='';$('#wscanmsg').textContent=t('dyn.scanning');
 let aps=null;
 for(let i=0;i<10;i++){let d;try{d=await gj('/api/wifi/scan');}catch(e){}
  if(d&&d.scanning===0){aps=d.aps;break;} await new Promise(r=>setTimeout(r,800));}
 if(!aps){$('#wscanmsg').textContent=t('dyn.scanfail');return;}
 $('#wscanmsg').textContent=aps.length+' '+t('dyn.apsfound');
 aps.sort((a,b)=>b.rssi-a.rssi).forEach(a=>{const li=document.createElement('li');
  li.textContent=a.ssid+'  ('+a.rssi+'dBm)';
  li.style.cssText='padding:7px 9px;border:1px solid var(--line);border-radius:6px;margin:3px 0;cursor:pointer';
  li.onclick=()=>{$('#wssid').value=a.ssid;$('#wpass').focus();};ul.appendChild(li);});}
$('#wscan').onclick=realScan;
$('#wsave').onclick=async()=>{const r=await post('/api/wifi',{ssid:$('#wssid').value,
 pass:$('#wpass').value,mdns:$('#wmdns').value,mode:$('#wmode').value});
 $('#wmsg').textContent=r.ok?t('dyn.wifisaved'):t('dyn.fail');};
// ---- P4 calib ----
$('#crun').onclick=async()=>{$('#cmsg').textContent=t('dyn.calibrating');
 const r=await post('/api/calib',{op:'run'});$('#cmsg').textContent=r.ok?t('dyn.exec'):(r.busy?t('dyn.busy'):t('dyn.fail'));};
$('#csave').onclick=async()=>{
 await post('/api/calib',{op:'indoor',val:$('#cin').value});
 await post('/api/calib',{op:'nf',val:$('#cnf').value});
 await post('/api/calib',{op:'wdth',val:$('#cwd').value});
 await post('/api/calib',{op:'srej',val:$('#csr').value});
 await post('/api/calib',{op:'minnum',val:$('#cmn').value});
 await post('/api/calib',{op:'mask',val:$('#cmk').checked?1:0});
 $('#csmsg').textContent=t('dyn.applied');};
$('#cclear').onclick=async()=>{const r=await post('/api/calib',{op:'clear'});
 $('#csmsg').textContent=r.ok?t('dyn.statclear'):t('dyn.fail');};
// スライダー値表示 + i アイコン吹き出し
const MN=[1,5,9,16];
function updSliders(){$('#cnfv').textContent=$('#cnf').value;$('#cwdv').textContent=$('#cwd').value;
 $('#csrv').textContent=$('#csr').value;$('#cmnv').textContent=MN[+$('#cmn').value]+' '+t('dyn.times');}
['cnf','cwd','csr','cmn'].forEach(id=>$('#'+id).addEventListener('input',updSliders));
$$('#p3 .info').forEach(b=>{b.type='button';b.onclick=(e)=>{e.preventDefault();e.stopPropagation();const tp=$('#tip-'+b.dataset.t);if(tp)tp.hidden=!tp.hidden;};});
$('#lclr').onclick=async()=>{if(!confirm(t('dyn.confirmclear')))return;
 await post('/api/logclear',{});const s=await gj('/api/settings');
 $('#lsz').textContent=logSizeText(s);};
// ---- P5 offset ----
$('#osave').onclick=async()=>{const r=await post('/api/offset',{t:$('#oft').value,h:$('#ofh').value});
 $('#omsg').textContent=r.ok?t('dyn.applied'):t('dyn.fail');};
// ---- P6 logcfg ----
$('#lsave').onclick=async()=>{
 const period=Math.max(1,parseInt($('#lp').value)||5);   // 空/不正は既定へフォールバック
 const drop=$('#ld').checked?1:0;
 let n=Math.max(1,parseInt($('#ln').value)||8); if(drop&&n<4)n=4;  // 除外時は自動でn>=4
 $('#lp').value=period;$('#ln').value=n;                  // 正規化した値をUIへ反映
 const r=await post('/api/logcfg',{period,drop,n});
 $('#lmsg').textContent=r.ok?(LANG==='ja'?('保存しました(永続) 周期'+period+'s/n'+n+(drop?'/最小最大除外':''))
   :('Saved (persistent): period '+period+'s / n'+n+(drop?' / drop min-max':''))):(r.err||t('dyn.fail'));
 if(r.ok){curCfg.period=period;updRetain();}};
// ---- アカウント(パスワード変更) ----
$('#absave').onclick=async()=>{$('#amsg').textContent='...';
 const r=await post('/api/passwd',{old:$('#aop').value,user:$('#au').value,'new':$('#anp').value}).catch(()=>({ok:0}));
 if(r&&r.ok){$('#amsg').textContent=t('dyn.pwchanged');$('#aop').value='';$('#anp').value='';$('#au').value='';setTimeout(showLogin,1200);}
 else{$('#amsg').textContent=(r&&r.err)||t('dyn.fail');}};
// ---- P7 サーバ連携 ----
async function loadServer(){try{const s=await gj('/api/server');
 $('#sen').checked=!!s.enable;$('#sbase').value=s.base||'';$('#sroot').value=s.root||'';$('#scode').value=s.code||'';
 $('#smac').textContent=s.mac||'--';$('#stok').textContent=s.hasToken?'✓':'--';$('#sstat').textContent=s.msg||'--';}catch(e){}}
function srvArgs(){return {enable:$('#sen').checked?1:0,base:$('#sbase').value,root:$('#sroot').value,code:$('#scode').value};}
$('#ssave').onclick=async()=>{const r=await post('/api/server',srvArgs()).catch(()=>({ok:0}));
 $('#smsg').textContent=r&&r.ok?t('dyn.applied'):t('dyn.fail');if(r)$('#stok').textContent=r.hasToken?'✓':'--';};
$('#senroll').onclick=async()=>{$('#smsg').textContent=t('dyn.enrolling');
 const a=srvArgs();a.action='enroll';a.enable=1;$('#sen').checked=true;
 await post('/api/server',a).catch(()=>{});
 for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,2000));
   const s=await gj('/api/server').catch(()=>null);
   if(s){$('#stok').textContent=s.hasToken?'✓':'--';$('#sstat').textContent=s.msg||'';
     if(s.hasToken){$('#smsg').textContent='OK';return;}}}
 $('#smsg').textContent=t('dyn.fail')+' ('+($('#sstat').textContent||'')+')';};
// ---- 言語切替 (設定ページ) ----
$('#lang').onchange=()=>{applyLang($('#lang').value);};
applyLang(LANG);   // 起動時に既定=英語(または保存言語)を適用
// 起動時: 認証済みなら設定ロード(=フォームに反映+updRetain+startPoll)、未認証なら
// ライブpollのみ開始(401でログイン表示→ログイン成功時に loadSettings)。
checkAuth().then(ok=>{ if(ok) loadSettings(); else startPoll(); });
</script></body></html>)HTML";

static void handleRoot(HttpCtx& server){
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "max-age=30");   // 弱電界での再読込を高速化(30s)
  server.send_P(200, "text/html; charset=utf-8", (const uint8_t*)WLB_PAGE_GZ, WLB_PAGE_GZ_LEN);
}

// ---- トランポリン: httpd_req → HttpCtx → ハンドラ。g_webMtx で全ハンドラを直列化
//      (旧 WebServer 同様に単一実行 = handleNow/handleHistory の静的バッファも保護)。----
typedef void (*WlbHandler)(HttpCtx&);
static esp_err_t trampoline(httpd_req_t* r, WlbHandler h) {
  if (g_webMtx) xSemaphoreTake(g_webMtx, portMAX_DELAY);
  esp_err_t rc;
  { HttpCtx ctx(r); h(ctx); rc = ctx.finish(); }
  if (g_webMtx) xSemaphoreGive(g_webMtx);
  return rc;
}
#define WLB_TRAMP(name, fn) static esp_err_t name(httpd_req_t* r){ return trampoline(r, fn); }
WLB_TRAMP(t_root,     handleRoot)      WLB_TRAMP(t_chart,   handleChartJs)
WLB_TRAMP(t_auth,     handleAuthStatus)WLB_TRAMP(t_login,   handleLogin)
WLB_TRAMP(t_logout,   handleLogout)    WLB_TRAMP(t_passwd,  handlePasswd)
WLB_TRAMP(t_now,      handleNow)       WLB_TRAMP(t_history, handleHistory)
WLB_TRAMP(t_settings, handleSettings)  WLB_TRAMP(t_wscan,   handleWifiScan)
WLB_TRAMP(t_wifi,     handleWifiSet)   WLB_TRAMP(t_calib,   handleCalib)
WLB_TRAMP(t_offset,   handleOffset)    WLB_TRAMP(t_logcfg,  handleLogcfg)
WLB_TRAMP(t_csv,      handleCsv)       WLB_TRAMP(t_logclear,handleLogClear)
WLB_TRAMP(t_srvget,   handleServerGet) WLB_TRAMP(t_srvset,  handleServerSet)

struct Route { const char* uri; httpd_method_t method; esp_err_t (*fn)(httpd_req_t*); };
static const Route ROUTES[] = {
  {"/",             HTTP_GET,  t_root},     {"/chart.min.js", HTTP_GET,  t_chart},
  {"/api/auth",     HTTP_GET,  t_auth},     {"/api/login",    HTTP_POST, t_login},
  {"/api/logout",   HTTP_POST, t_logout},   {"/api/passwd",   HTTP_POST, t_passwd},
  {"/api/now",      HTTP_GET,  t_now},      {"/api/history",  HTTP_GET,  t_history},
  {"/api/settings", HTTP_GET,  t_settings}, {"/api/wifi/scan",HTTP_GET,  t_wscan},
  {"/api/wifi",     HTTP_POST, t_wifi},     {"/api/calib",    HTTP_POST, t_calib},
  {"/api/offset",   HTTP_POST, t_offset},   {"/api/logcfg",   HTTP_POST, t_logcfg},
  {"/api/csv",      HTTP_GET,  t_csv},      {"/api/logclear", HTTP_POST, t_logclear},
  {"/api/server",   HTTP_GET,  t_srvget},   {"/api/server",   HTTP_POST, t_srvset},
};
static void registerRoutes(httpd_handle_t h) {
  for (auto& rt : ROUTES) { httpd_uri_t u = {}; u.uri = rt.uri; u.method = rt.method; u.handler = rt.fn;
    httpd_register_uri_handler(h, &u); }
}

void webui_begin(WlbSettings& settings) {
  if (started) return;
  g_set = &settings;
  applyIoConfig();
  g_webMtx = xSemaphoreCreateMutex();

  // HTTP(80): プレーン httpd
  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.server_port = 80; hc.ctrl_port = 32080; hc.max_uri_handlers = 20;
  hc.stack_size = 8192; hc.lru_purge_enable = true;
  if (httpd_start(&g_http, &hc) == ESP_OK) registerRoutes(g_http);
  else Serial.println("[web] HTTP start failed");

  // HTTPS(443): TLS httpd (自己署名 EC P-256)
  httpd_ssl_config_t sc = HTTPD_SSL_CONFIG_DEFAULT();
  sc.servercert     = (const uint8_t*)WLB_TLS_CERT; sc.servercert_len = sizeof(WLB_TLS_CERT);
  sc.prvtkey_pem    = (const uint8_t*)WLB_TLS_KEY;  sc.prvtkey_len    = sizeof(WLB_TLS_KEY);
  sc.port_secure = 443;
  sc.httpd.max_uri_handlers = 20; sc.httpd.stack_size = 10240;   // TLS はスタックを食う
  sc.httpd.ctrl_port = 32443;     sc.httpd.lru_purge_enable = true;
  if (httpd_ssl_start(&g_https, &sc) == ESP_OK) registerRoutes(g_https);
  else Serial.println("[web] HTTPS start failed");

  if (MDNS.begin(g_set->mdns)) { MDNS.addService("http","tcp",80); MDNS.addService("https","tcp",443); }
  started = true;
  Serial.printf("[web] up HTTP:80 + HTTPS:443  STA http://%s/  mDNS %s.local\n",
    WiFi.localIP().toString().c_str(), g_set->mdns);
}

void webui_loop() {
  // httpd は専用タスク駆動なので handleClient 不要。再起動予約のみ処理。
  if (g_rebootAt && (int32_t)(millis() - g_rebootAt) >= 0) { delay(50); ESP.restart(); }
}
