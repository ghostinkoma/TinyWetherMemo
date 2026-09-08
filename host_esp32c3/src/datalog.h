// ============================================================================
//  datalog.h  -  実データを LittleFS へ追記記録 (トレース) + CSV ダウンロード
//
//  loopTask から駆動。FS は I2C ではないので ioTask とは独立(不変条件を壊さない)。
//  g_state の最新スナップショットを測定周期毎に 1 行 CSV 追記。サイズ上限で
//  1 世代ローテート(/log.csv → /log.old.csv)。CSV DL は old→cur を連結して配信。
// ============================================================================
#ifndef WLB_DATALOG_H
#define WLB_DATALOG_H
#include <Arduino.h>
class HttpCtx;

bool     datalog_begin();                                   // LittleFS mount
// 分足(period×n平均)を1レコード追記。epoch>1.7e9=絶対時刻(当日ファイル), それ以外=起動相対秒
// (オフライン→/gpend.csv に "B<秒>")。temp/humi は AHT20 平均, thunder=danger%。
void     datalog_append(uint32_t epoch, float t, float h, uint8_t danger);
void     datalog_patch_boottime(uint32_t bootEpoch);        // NTP確定時: "B<秒>"→絶対時刻
void     datalog_stream_csv(HttpCtx& srv);                  // CSV ダウンロード配信
void     datalog_clear();                                   // ログ全消去
uint32_t datalog_bytes();                                   // 現在の総ログサイズ
uint32_t datalog_fs_total();                                // FS 総容量 (0=未mount)
uint32_t datalog_fs_used();                                 // FS 使用量

#endif // WLB_DATALOG_H
