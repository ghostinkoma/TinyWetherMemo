// ============================================================================
//  io_task.cpp  -  I2C 単独所有タスク (雷ブリッジ + 環境センサ + 設定/校正/履歴)
//  詳細は io_task.h / Docs/SPEC.md / ARCHITECTURE.md
// ============================================================================
#include "io_task.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_task_wdt.h"
#include <Wire.h>
#include "config.h"
#include "sensors.h"
#include "lightning.h"
#include "shared_state.h"
#ifdef WLB_OLED
  #include "display_oled.h"
#endif

static EnvSensors env;
static Lightning  ln;
#ifdef WLB_OLED
static OledDisplay oled;
#endif

// 起動時/ライブの生I2Cスキャン
static void scanBus(SystemSnapshot& s) {
  s.scanCount = 0;
  for (uint8_t a = 3; a < 0x78 && s.scanCount < 16; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) s.scanList[s.scanCount++] = a;
  }
}

// I2C バス自動リカバリ: スタックしたスレーブ(SDA固着/クロックストレッチ hang)を
// SCL 9 クロックの手動トグルで解放し、STOP を生成して Wire を再初期化する。
// 稼働中に Error263(timeout) が連発しバスが wedge した時の自己回復。
static void recoverI2C() {
  Serial.println("[io] I2C bus recovery: 9-clock unstick + reinit");
  Wire.end();
  pinMode(WLB_I2C_SCL, OUTPUT_OPEN_DRAIN);
  pinMode(WLB_I2C_SDA, INPUT_PULLUP);
  for (int i = 0; i < 9; i++) {                 // スタックしたスレーブへ 9 クロック
    digitalWrite(WLB_I2C_SCL, LOW);  delayMicroseconds(6);
    digitalWrite(WLB_I2C_SCL, HIGH); delayMicroseconds(6);
    if (digitalRead(WLB_I2C_SDA)) break;        // SDA 解放されたら終了
  }
  pinMode(WLB_I2C_SDA, OUTPUT_OPEN_DRAIN);      // STOP 条件 (SDA: L→H, SCL=H)
  digitalWrite(WLB_I2C_SDA, LOW);  delayMicroseconds(6);
  digitalWrite(WLB_I2C_SCL, HIGH); delayMicroseconds(6);
  digitalWrite(WLB_I2C_SDA, HIGH); delayMicroseconds(6);
  Wire.begin(WLB_I2C_SDA, WLB_I2C_SCL, WLB_I2C_HZ);
  Wire.setTimeOut(WLB_I2C_TIMEOUT_MS);          // 再init で戻るので再設定
  env.retryAbsent();                            // 再検出 (env.begin は二重add不可なので使わない)
  ln.begin();                                   // 雷ブリッジ通信を再初期化
}

// AS3935 校正結果を snapshot へ取り込む
static void readCalib(SystemSnapshot& s) {
  TSCalib c;
  if (ln.raw().readCalib(c)) {
    s.calib.tuncap = c.tuncap; s.calib.ok = c.ok;
    s.calib.count = c.count;   s.calib.target = c.target;
    s.calib.valid = true;
  }
}

// Web からの校正/感度コマンドを実行 (I2C はここだけ)
static void execCmd(uint8_t op, int32_t a, SystemSnapshot& cur) {
  switch (op) {
    case IOCMD_RECAL:      ln.raw().recalibrate();          break;
    case IOCMD_GAIN:       ln.raw().setGain(a != 0);        break;
    case IOCMD_NOISEFLOOR: ln.raw().setNoiseFloor((uint8_t)a); break;
    case IOCMD_WATCHDOG:   ln.raw().setWatchdog((uint8_t)a);   break;
    case IOCMD_SREJ:       ln.raw().setSpikeRejection((uint8_t)a); break;
    case IOCMD_MINNUM:     ln.raw().setMinLightnings((uint8_t)a);  break;
    case IOCMD_MASKDIST:   ln.raw().setMaskDisturbers(a != 0);     break;
    case IOCMD_CLEARSTATS: ln.raw().clearStats();           break;
    default: return;
  }
  readCalib(cur);   // 反映後に結果を更新
}

// n サンプル取得し、必要なら最小最大を捨てて平均 (1チャンネル)
static float avgDrop(float* v, uint8_t n, bool drop) {
  if (n == 0) return NAN;
  if (drop && n >= 3) {
    float mn = v[0], mx = v[0]; uint8_t imn = 0, imx = 0;
    for (uint8_t i = 1; i < n; i++) { if (v[i] < mn){mn=v[i];imn=i;} if (v[i] > mx){mx=v[i];imx=i;} }
    float sum = 0; uint8_t cnt = 0;
    for (uint8_t i = 0; i < n; i++) { if (i==imn||i==imx) continue; sum += v[i]; cnt++; }
    return cnt ? sum / cnt : NAN;
  }
  float sum = 0; for (uint8_t i = 0; i < n; i++) sum += v[i]; return sum / n;
}

static void ioTask(void*) {
  bool eok = env.begin();
  bool lok = ln.begin();
  Wire.setTimeOut(WLB_I2C_TIMEOUT_MS);   // ★389B束読み(poll)が既定50msで timeout する対策
#ifdef WLB_OLED
  bool ook = oled.begin();
  Serial.printf("[io] sensors=%u bridge=%s oled=%s\n", env.count(), lok?"ok":"NG", ook?"ok":"NG");
#else
  Serial.printf("[io] sensors=%u bridge=%s\n", env.count(), lok?"ok":"NG");
#endif
  (void)eok;

  SystemSnapshot cur;
  scanBus(cur);
  readCalib(cur);
  // 初回即読み(5秒待たずに値を出す)
  cur.nSamples = env.sample(cur.samples, WLB_MAX_SENSORS);
  cur.env = env.merged();

  uint32_t lastSample = 0, lastScan = 0, lastRetry = 0, lastLnRetry = 0;
  uint8_t  emptyScans = 0;      // wedge 連続検出回数
  bool     busOk = true;        // バス健全 (スキャン応答あり かつ wedge でない)
  bool     bridgeWasOk = false; // ブリッジが一度でも read 成功したか (未接続との区別)
  uint8_t  recTries = 0;        // 連続リカバリ試行回数
  bool     i2cGaveUp = false;   // リカバリ断念 (ブリッジ不在扱い→センサは継続動作)
#ifdef WLB_OLED
  uint32_t lastOled = 0;
#endif
  uint32_t lastEpoch = 0, lastSyncMs = 0;
  bool haveEpoch = false, haveBoot = false; uint8_t lastBootId = 0;

  IoConfig cfg = g_state.getIoConfig();

#if WLB_ENABLE_WDT
  esp_task_wdt_add(NULL);   // WDT: ioTask を監視 (I2Cハング等を検出→パニック→再起動)
#endif

  for (;;) {
#if WLB_ENABLE_WDT
    esp_task_wdt_reset();   // WDT feed
#endif
    uint32_t now = millis();

    // 時刻同期依頼
    uint32_t ep;
    if (g_state.takeTimeSync(ep)) { ln.syncTime(ep); lastEpoch=ep; lastSyncMs=now; haveEpoch=true; }

    // Web からの校正/感度コマンド
    uint8_t op; int32_t a;
    if (g_state.takeCmd(op, a)) execCmd(op, a, cur);

    // ライブ I2C 再スキャン (2秒) + read wedge 検出。
    //  スレーブ・スタックは write(scan) は ACK するが read(write→repeated-START→read;
    //  i2cWriteReadNonStop) が timeout(263) になる。雷ブリッジの readStatus 成否を反映する
    //  bridgeOk を read 健全性信号として使い、連続 NG でバスリカバリを発動する。
    if (now - lastScan >= 2000) {
      lastScan = now; scanBus(cur);
      bool bridgeRead = ln.snapshot().bridgeOk;          // tick(B)で更新済のread健全性
      if (bridgeRead) { bridgeWasOk = true; recTries = 0; i2cGaveUp = false; }
      // wedge = 一度OKになった後に read NG (バス固着)。未接続(最初からNG)は wedge 扱いしない。
      bool wedge = bridgeWasOk && !bridgeRead && !i2cGaveUp;
      busOk = (cur.scanCount > 0) && !wedge;
      if (wedge) {
        if (++emptyScans >= WLB_I2C_RECOVER_SCANS) {
          emptyScans = 0;
          if (++recTries > WLB_I2C_RECOVER_MAX) { i2cGaveUp = true; Serial.println("[io] I2C recovery gave up (bridge dead?)"); }
          else recoverI2C();
        }
      } else emptyScans = 0;
    }
    // 未検出センサ再init (3秒) — バス生存時のみ
    if (busOk && now - lastRetry >= 3000) { lastRetry = now; env.retryAbsent(); }
    // 雷ブリッジ未接続なら再init (3秒) — バス生存時のみ
    if (busOk && now - lastLnRetry >= 3000 && !cur.ln.bridgeOk) { lastLnRetry = now; ln.begin(); }

    // 雷 drain + 危険度 (高頻度)。バス wedge 中はドレインを止め、Error263 洪水を抑制。
#if WLB_ENABLE_LN
    if (busOk) ln.tick(now);
#endif
    cur.ln = ln.snapshot();
    if (haveBoot && cur.ln.bootId != lastBootId && haveEpoch) {
      uint32_t est = lastEpoch + (now - lastSyncMs)/1000; ln.syncTime(est);
      lastEpoch = est; lastSyncMs = now;
    }
    lastBootId = cur.ln.bootId; haveBoot = true;

    // ★測定モデル: 5秒ごとに「1回測定」→ダッシュボード/ライブ足。
    //   その 5秒値を n 個ためて最小最大を捨て平均 → period×n(既定60s) ごとに 1 レコード
    //   (=分足)を FS へ保存。分足の epoch は絶対(NTP)。これにより FS を長期保存に最適化。
    if (busOk && now - lastSample >= cfg.samplePeriodMs) {
      lastSample = now;
      cfg = g_state.getIoConfig();                 // 最新設定を反映
      // --- 1回測定 (単発読み + 妥当性 + オフセット) ---
      cur.nSamples = env.sample(cur.samples, WLB_MAX_SENSORS);
      EnvReading m = env.merged();
      EnvReading e;
      if (m.tValid) { e.tempC = m.tempC + cfg.offT; e.tValid = true; }
      if (m.hValid) { e.rh    = m.rh    + cfg.offH; e.hValid = true; }
      if (m.pValid && m.presHpa > 800.0f && m.presHpa < 1085.0f) { e.presHpa = m.presHpa; e.pValid = true; }
      e.ok = e.tValid || e.hValid || e.pValid;
      cur.env = e;

      uint32_t sec = now/1000;
      uint32_t absSec = haveEpoch ? (lastEpoch + (now - lastSyncMs)/1000) : sec;
      // ライブ足(5秒, 30分) リング (圧縮格納)
      HistSample hs; hs.epoch = sec;
      hs.t = e.tValid?wlbEnc2(e.tempC):WLB_NA; hs.h = e.hValid?wlbEnc2(e.rh):WLB_NA; hs.p = e.pValid?wlbEncP(e.presHpa):WLB_NA;
      hs.danger = cur.ln.dangerPct; hs.lTotal = cur.ln.lightningTotal; hs.dTotal = cur.ln.disturberTotal;
      g_state.histAppend(hs);

      // --- FS集計: n 個の 5秒値を貯め、最小最大除外→平均 → period×n ごとに分足レコード ---
      static float gt[20], gh[20], gp[20]; static uint8_t gnt=0, gnh=0, gnp=0, gcnt=0, gdMax=0;
      uint8_t N = cfg.avgN; if (N < 1) N = 1; if (N > 20) N = 20;
      if (e.tValid && gnt < 20) gt[gnt++] = e.tempC;
      if (e.hValid && gnh < 20) gh[gnh++] = e.rh;
      if (e.pValid && gnp < 20) gp[gnp++] = e.presHpa;
      if (cur.ln.dangerPct > gdMax) gdMax = cur.ln.dangerPct;   // 危険度は期間内ピーク
      gcnt++;
      if (gcnt >= N) {
        HistSample ls; ls.epoch = absSec;                       // 分足レコードの絶対時刻
        ls.t = gnt?wlbEnc2(avgDrop(gt,gnt,cfg.dropMinMax)):WLB_NA;
        ls.h = gnh?wlbEnc2(avgDrop(gh,gnh,cfg.dropMinMax)):WLB_NA;
        ls.p = gnp?wlbEncP(avgDrop(gp,gnp,cfg.dropMinMax)):WLB_NA;
        ls.danger = gdMax; ls.lTotal = cur.ln.lightningTotal; ls.dTotal = cur.ln.disturberTotal;
        g_state.histAppendMin(ls);                              // 分足リング(RAM)+minSeq++ → loopがFS保存
        gcnt = gnt = gnh = gnp = 0; gdMax = 0;
      }
    }

    // 診断文字列: 登録センサ毎に present(+/x) と直近値, scan台数。Webで読取切り分け。
    {
      char* p = cur.dbg; int rem = sizeof(cur.dbg);
      int w = snprintf(p, rem, "reg%u scan%u:", env.count(), cur.scanCount); p+=w; rem-=w;
      for (uint8_t i=0;i<env.count() && rem>12;i++){
        IEnvSensor* s = env.at(i);
        w = snprintf(p, rem, " %s%c", s->name(), s->present()?'+':'x'); p+=w; rem-=w;
      }
    }
    cur.updatedMs = now;
    g_state.set(cur);

#ifdef WLB_OLED
    if (now - lastOled >= WLB_OLED_MS) { lastOled = now; oled.render(cur, g_state.getNet()); }
#endif
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void iotask_start() { xTaskCreate(ioTask, "io", 8192, nullptr, 1, nullptr); }
