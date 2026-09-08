// ============================================================================
//  histfs.cpp  -  詳細は histfs.h
// ============================================================================
#include "histfs.h"
#include <LittleFS.h>
#include <string.h>
#include "config.h"

static const size_t REC = sizeof(HistSample);   // 圧縮レコード 15B

static uint32_t fileRecs(const char* p) {
  File f = LittleFS.open(p, "r"); if (!f) return 0;
  uint32_t n = (uint32_t)(f.size() / REC); f.close(); return n;
}

void histfs_append(const HistSample& s) {
  File f = LittleFS.open(WLB_HISTFS_PATH, "a");
  if (!f) return;
  if (f.size() >= (size_t)WLB_HISTFS_CAP * REC) {   // 上限 → 1世代退避
    f.close();
    LittleFS.remove(WLB_HISTFS_OLD);
    LittleFS.rename(WLB_HISTFS_PATH, WLB_HISTFS_OLD);
    f = LittleFS.open(WLB_HISTFS_PATH, "a");
    if (!f) return;
  }
  f.write((const uint8_t*)&s, REC);
  f.close();
}

// old→cur を順に走査し epoch>=since のレコードへ cb を呼ぶ。件数を返す。
// ※ ブロック読み+memcpy で 15B 境界の非アラインアクセスを回避 (RISC-V C3)。
typedef void (*RecCb)(const HistSample&, void*);
static uint32_t scan(uint32_t since, RecCb cb, void* arg) {
  const char* paths[2] = { WLB_HISTFS_OLD, WLB_HISTFS_PATH };
  uint32_t cnt = 0;
  for (int pi = 0; pi < 2; pi++) {
    File f = LittleFS.open(paths[pi], "r"); if (!f) continue;
    uint8_t buf[REC * 32];
    int got;
    while ((got = f.read(buf, sizeof buf)) > 0) {
      int m = got / (int)REC;
      for (int i = 0; i < m; i++) {
        HistSample r; memcpy(&r, buf + i * REC, REC);
        if (r.epoch >= since) { cnt++; if (cb) cb(r, arg); }
      }
      if (got < (int)sizeof buf) break;
    }
    f.close();
  }
  return cnt;
}

struct QCtx { HistSample* out; uint16_t max; uint16_t n; uint32_t idx; uint16_t stride; };
static void emitCb(const HistSample& r, void* a) {
  QCtx* q = (QCtx*)a;
  if (q->idx % q->stride == 0 && q->n < q->max) q->out[q->n++] = r;
  q->idx++;
}

uint16_t histfs_query(uint32_t since, HistSample* out, uint16_t maxN) {
  if (maxN == 0) return 0;
  uint32_t total = scan(since, nullptr, nullptr);          // 1st pass: 件数
  if (total == 0) return 0;
  uint16_t stride = (total > maxN) ? (uint16_t)((total + maxN - 1) / maxN) : 1;
  QCtx q{ out, maxN, 0, 0, stride };
  scan(since, emitCb, &q);                                 // 2nd pass: 間引き取得
  return q.n;
}

struct SCtx { uint32_t skip; uint32_t idx; };
static void seedCb(const HistSample& r, void* a) {
  SCtx* s = (SCtx*)a;
  if (s->idx >= s->skip) g_state.histAppendMin(r);         // 末尾 CAP 件を RAM 分足リングへ復元
  s->idx++;
}

void histfs_seed() {
  uint32_t total = fileRecs(WLB_HISTFS_OLD) + fileRecs(WLB_HISTFS_PATH);
  if (total == 0) return;
  SCtx s{ (total > WLB_TIER_CAP) ? (total - WLB_TIER_CAP) : 0, 0 };
  scan(0, seedCb, &s);
}

uint32_t histfs_count() {
  return fileRecs(WLB_HISTFS_OLD) + fileRecs(WLB_HISTFS_PATH);
}

void histfs_clear() {
  LittleFS.remove(WLB_HISTFS_PATH);
  LittleFS.remove(WLB_HISTFS_OLD);
}
