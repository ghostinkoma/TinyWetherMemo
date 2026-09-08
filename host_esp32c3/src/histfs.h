// ============================================================================
//  histfs.h / histfs.cpp  -  時足(1時間代表値)を LittleFS へ永続化する長期履歴
//
//  目的: RAM リング(時足=最大約5日)は再起動で消える。日/週/月足を再起動をまたいで
//        提供するため、時足の代表値(圧縮HistSample=15B)を FS に追記保存する。
//  時刻: epoch は必ず絶対(NTP)。未同期の時足は main 側で永続化しない。
//  所有: 全 API を loopTask からのみ呼ぶこと(datalog と同じ。LittleFS の競合回避)。
//        ※ ioTask(I2C単独所有)からは絶対に呼ばない。
//  格納: /hist.bin へ append。WLB_HISTFS_CAP 超で /hist.old.bin へ1世代退避。
// ============================================================================
#ifndef WLB_HISTFS_H
#define WLB_HISTFS_H
#include <Arduino.h>
#include "shared_state.h"   // HistSample (packed 15B)

void     histfs_seed();                        // FS の末尾を RAM 時足リングへ復元 (setup)
void     histfs_append(const HistSample& s);   // 時足 1 点を FS へ追記 (loopTask)
// [sinceEpoch, 現在] のレコードを最大 maxN 点へ間引いて out(古→新)へ。返り値=点数。
uint16_t histfs_query(uint32_t sinceEpoch, HistSample* out, uint16_t maxN);
uint32_t histfs_count();                       // 総レコード数 (old+cur)
void     histfs_clear();                        // FS長期履歴を全消去 (Web「ログ消去」から)

#endif // WLB_HISTFS_H
