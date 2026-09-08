// ============================================================================
//  web_httpd.h  -  esp_http_server(httpd) を Arduino WebServer 風に扱う薄いシム。
//
//  目的: HTTP(80) と HTTPS(443, TLS) の 2 つの httpd インスタンスに同一ハンドラを
//        登録するため、既存の WebServer ベースのハンドラ本体をほぼ無改造で使えるよう
//        arg()/hasArg()/header()/send()/sendHeader()/sendContent() を提供する。
//        ※ トランポリンで g_webMutex により全ハンドラを直列化(旧 WebServer 同様)。
// ============================================================================
#pragma once
#include <Arduino.h>
#include <vector>
#include "esp_http_server.h"

class HttpCtx {
public:
  explicit HttpCtx(httpd_req_t* r);

  bool   hasArg(const String& k) const;
  String arg(const String& k) const;
  bool   hasHeader(const char* k) const;
  String header(const char* k) const;

  void   sendHeader(const char* k, const String& v);       // send/send_P より前に呼ぶ
  void   send(int code, const char* ct, const String& body);
  void   send_P(int code, const char* ct, const uint8_t* data, size_t len);
  void   setContentLength(int) {}                          // httpd はチャンク送信なので no-op
  void   sendContent(const char* s);                       // 空文字 = チャンク終端
  void   sendContent(const char* s, size_t n);
  void   sendContent(const String& s) { sendContent(s.c_str(), s.length()); }

  esp_err_t finish();                                      // トランポリンが最後に呼ぶ

private:
  httpd_req_t* _req;
  std::vector<std::pair<String,String>> _params;
  std::vector<std::pair<String,String>> _hdrs;             // send まで生存させる
  int  _state = 0;                                         // 0=未送信 1=完了 2=チャンク中 3=終端済
  void applyHead(int code, const char* ct);
};
