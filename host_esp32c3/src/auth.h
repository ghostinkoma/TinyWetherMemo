// ============================================================================
//  auth.h / auth.cpp  -  ログイン認証 (SHA-256 パスワードハッシュ + セッションCookie)
//
//  ESP32 のセキュリティ・ペリフェラルを活用:
//    - SHA-256 : mbedtls_sha256 (ESP32 HW SHA アクセラレータ) でパスワードをソルト付き
//                ハッシュ化して保存(平文非保持)。
//    - RNG     : esp_random() (ESP32 HW 乱数; RF ON 時は真性乱数) でソルト/セッション
//                トークンを生成。
//  単一ユーザー(家庭用ロガー想定)。永続化は LittleFS /auth.ini。セッションはRAM。
// ============================================================================
#ifndef WLB_AUTH_H
#define WLB_AUTH_H
#include <Arduino.h>

namespace wauth {
  void   begin();                                   // /auth.ini ロード or 既定シード
  bool   check(const String& user, const String& pass);
  String issue(const String& user);                 // セッション発行 → トークン
  bool   validate(const String& token);             // 有効性確認 + スライディング延長
  void   revoke(const String& token);               // ログアウト
  String userOf(const String& token);               // トークン → ユーザー名
  // 認証情報変更 (旧パス照合 → 新ユーザー/パス)。全セッション失効。
  bool   changeCredentials(const String& oldPass, const String& newUser,
                           const String& newPass, String& err);
}
#endif // WLB_AUTH_H
