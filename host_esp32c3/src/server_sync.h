// ============================================================================
//  server_sync.h  -  SQLサーバ連携 (端末→サーバ PUSH)。詳細は server_sync.cpp
// ----------------------------------------------------------------------------
//  スタンドアロン時(srvEnable=0)は一切通信しない。サーバ対応時のみ:
//   - enroll : {mac, code} を POST し token(=HMAC(secret,MAC)) を取得して保存
//   - push   : 分足レコード(温度/湿度/気圧/雷)を Bearer token 付きで ingest へ送信
//  MAC を鍵にした決定論トークンなので、一度 enroll すれば再取得不要。
// ============================================================================
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "shared_state.h"   // HistSample / wlbDec2 / wlbDecP

namespace srv {
  void   begin(WlbSettings* s);
  String deviceMac();                 // 正規化 "aa:bb:cc:dd:ee:ff"
  bool   hasToken();
  bool   enabled();
  bool   enroll(String& err);         // srvBase/srvRoot/srvCode を使い token 取得→保存
  bool   push(const HistSample& ls);  // 分足1点を送信 (要 token / 接続 / 絶対epoch)
  String lastMsg();                   // 直近の結果 (UI 表示用)
}
