// ============================================================================
//  datalog.cpp  -  FS(LittleFS) 実データログ = 日付名の日別ファイル再帰リング
//
//  ★圧縮: ファイル名を日付 `/g<YYYY-MM-DD>.csv`(JST暦日) にし、行内は **時刻のみ**
//    `HH:MM:SS,temp,humi,thunder` を保存 → 各行から日付(11文字)を省く。
//    ダウンロード時にファイル名の日付を各行へ付与し `YYYY-MM-DD HH:MM:SS,...` に復元する。
//  1日=1ファイル。分足(period×n平均)ごとに当日ファイルへ 1 行追記。総容量/日数の上限
//  超過で「最古日ファイル」を再帰削除(単位=1日)。クリアは Web「ログ消去」(+histfs)。
//  NTP未同期は /gpend.csv に `B<起動秒>,...` で記録し、NTP確定時に該当日ファイルへ振り分け。
// ============================================================================
#include "datalog.h"
#include <LittleFS.h>
#include "web_httpd.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_task_wdt.h"
#include "config.h"

static const char* HDR = "time,temp,humi,thunder\n";
static bool g_ok = false;

bool datalog_begin() { g_ok = LittleFS.begin(true); return g_ok; }

// --- 日付/時刻/ファイル名ユーティリティ (JST) ------------------------------
static void dateStr(uint32_t epoch, char* buf/*>=11*/) { time_t t=epoch; struct tm tm; localtime_r(&t,&tm); strftime(buf,11,"%Y-%m-%d",&tm); }
static void timeStr(uint32_t epoch, char* buf/*>=9*/)  { time_t t=epoch; struct tm tm; localtime_r(&t,&tm); strftime(buf,9,"%H:%M:%S",&tm); }
static void dayPath(const char* date, char* buf, size_t n) { snprintf(buf,n,"%s%s.csv",WLB_LOG_DAYPREFIX,date); }
// 名前 "g<YYYY-MM-DD>.csv"(先頭"/"有無) → 日付10桁 or 空。
static String fileDate(const String& name) {
  int i = name.lastIndexOf('/'); String b = (i>=0)?name.substring(i+1):name;
  if (!b.startsWith("g") || !b.endsWith(".csv")) return String();
  String d = b.substring(1, b.length()-4);
  if (d.length()!=10 || d[4]!='-' || d[7]!='-') return String();   // "gpend.csv" 等を除外
  return d;
}

// 日別ログを走査。totalBytes を集計し、最古(min 日付文字列=辞書順で時系列)を返す。
static String scanDays(uint32_t* totalBytes, int* count) {
  uint32_t total=0; String oldest; int c=0;
  File root = LittleFS.open("/");
  if (root) {
    for (File f=root.openNextFile(); f; f=root.openNextFile()) {
      String d = fileDate(String(f.name()));
      if (d.length()) { total += (uint32_t)f.size(); c++; if (oldest.length()==0 || d < oldest) oldest = d; }
    }
    root.close();
  }
  if (totalBytes) *totalBytes = total; if (count) *count = c;
  return oldest;
}

// 容量/日数の上限超過中は「最古日」を削除して再帰利用する。
static void recycle() {
  for (int g=0; g<WLB_LOG_MAXDAYS+8; g++) {
    uint32_t total; int cnt; String oldest = scanDays(&total,&cnt);
    if (cnt==0) return;
    if (total<=WLB_LOG_BUDGET && cnt<=WLB_LOG_MAXDAYS) return;
    char p[24]; dayPath(oldest.c_str(), p, sizeof p); LittleFS.remove(p);
  }
}

static void fmtTH(float t, float h, char* bt, char* bh) {
  auto v = [](bool ok, float x, char* buf, int d){ if (ok) dtostrf(x,0,d,buf); else { buf[0]='-'; buf[1]='-'; buf[2]=0; } };
  v(!isnan(t), t, bt, 2); v(!isnan(h), h, bh, 1);
}

// 分足(period×n平均)を1レコード追記(時刻のみ行)。
void datalog_append(uint32_t epoch, float t, float h, uint8_t danger) {
  if (!g_ok) return;
  char bt[12], bh[12]; fmtTH(t,h,bt,bh);
  char line[48];
  if (epoch > 1700000000UL) {                        // NTP 確定 → 当日ファイルへ (時刻のみ)
    char ds[11], ts[9]; dateStr(epoch,ds); timeStr(epoch,ts);
    int nn = snprintf(line, sizeof line, "%s,%s,%s,%u\n", ts, bt, bh, danger); if (nn<=0) return;
    char p[24]; dayPath(ds, p, sizeof p);
    File f = LittleFS.open(p, "a"); if (!f) return;
    f.write((const uint8_t*)line, nn); f.close();
    recycle();
  } else {                                            // オフライン → /gpend.csv (B<起動秒>)
    int nn = snprintf(line, sizeof line, "B%lu,%s,%s,%u\n", (unsigned long)epoch, bt, bh, danger); if (nn<=0) return;
    File f = LittleFS.open(WLB_LOG_PENDING, "a"); if (!f) return;
    f.write((const uint8_t*)line, nn); f.close();
  }
}

// NTP 初確定時: /gpend.csv の "B<秒>,..." を絶対時刻へ変換し、該当日ファイルへ時刻のみ行で振り分け。
void datalog_patch_boottime(uint32_t bootEpoch) {
  if (!g_ok || !LittleFS.exists(WLB_LOG_PENDING)) return;
  File in = LittleFS.open(WLB_LOG_PENDING, "r"); if (!in) return;
  char line[96]; size_t li=0;
  auto flush = [&]() {
    line[li] = 0;
    if (line[0]=='B') {
      unsigned long sec = strtoul(line+1, nullptr, 10);
      const char* comma = strchr(line, ',');             // ",temp,humi,thunder"
      uint32_t abs = bootEpoch + (uint32_t)sec;
      char ds[11], ts[9]; dateStr(abs,ds); timeStr(abs,ts);
      char p[24]; dayPath(ds, p, sizeof p);
      File o = LittleFS.open(p, "a");
      if (o) { o.print(ts); if (comma) o.print(comma); o.print('\n'); o.close(); }
    }
    li = 0;
  };
  while (in.available()) { int c=in.read(); if (c=='\n') flush(); else if (c=='\r') {} else if (li<sizeof(line)-1) line[li++]=(char)c; }
  if (li>0) flush();
  in.close(); LittleFS.remove(WLB_LOG_PENDING); recycle();
}

// 生ファイルをそのまま配信(gpend 用)。
static void streamRaw(HttpCtx& srv, const char* path) {
  File f = LittleFS.open(path, "r"); if (!f) return;
  uint8_t buf[512]; uint16_t chunks=0;
  while (f.available()) { int n=f.read(buf,sizeof buf); if (n>0) srv.sendContent((const char*)buf,n);
    if ((++chunks & 0x0F)==0) {
#if WLB_ENABLE_WDT
      esp_task_wdt_reset();
#endif
      delay(1);
    } }
  f.close();
}

// 日別ファイルを配信: 各行(時刻のみ)にファイル名の日付を付与 → "date time,rest\n"。
static void streamDay(HttpCtx& srv, const char* path, const char* date) {
  File f = LittleFS.open(path, "r"); if (!f) return;
  size_t dlen = strlen(date);
  char out[576]; int oi=0; char line[96]; int li=0; uint16_t rows=0;
  auto flushOut = [&]() { if (oi>0) { srv.sendContent(out, oi); oi=0; } };
  auto emit = [&]() {
    if (oi + (int)dlen + 2 + li > (int)sizeof(out)) flushOut();
    memcpy(out+oi, date, dlen); oi+=dlen; out[oi++]=' ';
    memcpy(out+oi, line, li);   oi+=li;   out[oi++]='\n';
    li = 0;
    if ((++rows & 0x7F)==0) {
#if WLB_ENABLE_WDT
      esp_task_wdt_reset();
#endif
      delay(1);
    }
  };
  while (f.available()) { int c=f.read(); if (c=='\n') { if (li>0) emit(); } else if (c=='\r') {} else if (li<(int)sizeof(line)-1) line[li++]=(char)c; }
  if (li>0) emit();
  flushOut(); f.close();
}

// ダウンロード: ヘッダ + 全日別ファイルを日付昇順で(日付付与し)連結 + 未同期(gpend)。
void datalog_stream_csv(HttpCtx& srv) {
  srv.sendHeader("Content-Disposition", "attachment; filename=wetherlog.csv");
  srv.send(200, "text/csv; charset=utf-8", "");
  srv.sendContent(HDR);
  if (g_ok) {
    // 全日付を1回だけ収集 → メモリ内で昇順ソート(文字列比較=時系列) → 順に配信。
    static char dates[WLB_LOG_MAXDAYS + 8][11]; int cnt=0;
    File root = LittleFS.open("/");
    if (root) {
      for (File f=root.openNextFile(); f && cnt<(int)(sizeof(dates)/sizeof(dates[0])); f=root.openNextFile()) {
        String d = fileDate(String(f.name())); if (d.length()) { strncpy(dates[cnt], d.c_str(), 10); dates[cnt][10]=0; cnt++; }
      }
      root.close();
    }
    for (int i=1;i<cnt;i++) { char key[11]; strcpy(key,dates[i]); int j=i-1;
      while (j>=0 && strcmp(dates[j],key)>0) { strcpy(dates[j+1],dates[j]); j--; } strcpy(dates[j+1],key); }
    for (int i=0;i<cnt;i++) { char p[24]; dayPath(dates[i], p, sizeof p); streamDay(srv, p, dates[i]); }
    streamRaw(srv, WLB_LOG_PENDING);   // 未同期(B<秒>)分があれば末尾に(そのまま)
  }
  srv.sendContent("");
}

// クリアは "g...csv"(新: 日付名, 旧: 整数名, gpend すべて)を広く消去する。
static bool isLogName(const String& name) {
  int i = name.lastIndexOf('/'); String b = (i>=0)?name.substring(i+1):name;
  return b.startsWith("g") && b.endsWith(".csv");
}
void datalog_clear() {
  if (!g_ok) return;
  static String names[WLB_LOG_MAXDAYS + 16]; int cnt=0;
  File root = LittleFS.open("/");
  if (root) {
    for (File f=root.openNextFile(); f && cnt<(int)(sizeof(names)/sizeof(names[0])); f=root.openNextFile()) {
      String n = String(f.name()); if (isLogName(n)) { if (!n.startsWith("/")) n = "/" + n; names[cnt++] = n; }
    }
    root.close();
  }
  for (int i=0;i<cnt;i++) LittleFS.remove(names[i]);
}

uint32_t datalog_bytes() {
  if (!g_ok) return 0;
  uint32_t total=0; scanDays(&total, nullptr);
  File pf = LittleFS.open(WLB_LOG_PENDING, "r"); if (pf) { total += (uint32_t)pf.size(); pf.close(); }
  return total;
}

uint32_t datalog_fs_total() { return g_ok ? (uint32_t)LittleFS.totalBytes() : 0; }
uint32_t datalog_fs_used()  { return g_ok ? (uint32_t)LittleFS.usedBytes()  : 0; }
