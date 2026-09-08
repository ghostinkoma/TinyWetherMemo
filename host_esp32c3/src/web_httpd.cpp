// ============================================================================
//  web_httpd.cpp  -  詳細は web_httpd.h
// ============================================================================
#include "web_httpd.h"

// ---- URL デコード (%XX / +) ----
static String urldecode(const String& s) {
  String o; o.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') o += ' ';
    else if (c == '%' && i + 2 < s.length()) {
      auto hx = [](char h)->int{ if(h>='0'&&h<='9')return h-'0'; if(h>='a'&&h<='f')return h-'a'+10;
        if(h>='A'&&h<='F')return h-'A'+10; return -1; };
      int hi = hx(s[i+1]), lo = hx(s[i+2]);
      if (hi >= 0 && lo >= 0) { o += (char)((hi<<4)|lo); i += 2; } else o += c;
    } else o += c;
  }
  return o;
}

// "k=v&k2=v2" を _params へ (URLデコード込み)
static void parseKV(const String& body, std::vector<std::pair<String,String>>& out) {
  int i = 0, n = body.length();
  while (i < n) {
    int amp = body.indexOf('&', i); if (amp < 0) amp = n;
    int eq = body.indexOf('=', i);
    if (eq >= 0 && eq < amp) {
      String k = body.substring(i, eq);
      String v = body.substring(eq + 1, amp);
      out.push_back({ urldecode(k), urldecode(v) });
    } else if (amp > i) {
      out.push_back({ urldecode(body.substring(i, amp)), String() });
    }
    i = amp + 1;
  }
}

HttpCtx::HttpCtx(httpd_req_t* r) : _req(r) {
  // クエリ文字列
  size_t qlen = httpd_req_get_url_query_len(r);
  if (qlen > 0 && qlen < 1024) {
    char* q = (char*)malloc(qlen + 1);
    if (q) { if (httpd_req_get_url_query_str(r, q, qlen + 1) == ESP_OK) { q[qlen] = 0; parseKV(String(q), _params); } free(q); }
  }
  // POST ボディ (form)
  if (r->content_len > 0 && r->content_len < 2048) {
    char* b = (char*)malloc(r->content_len + 1);
    if (b) {
      int got = 0;
      while (got < (int)r->content_len) {
        int rc = httpd_req_recv(r, b + got, r->content_len - got);
        if (rc <= 0) break; got += rc;
      }
      b[got] = 0; parseKV(String(b), _params); free(b);
    }
  }
}

bool HttpCtx::hasArg(const String& k) const {
  for (auto& p : _params) if (p.first == k) return true; return false;
}
String HttpCtx::arg(const String& k) const {
  for (auto& p : _params) if (p.first == k) return p.second; return String();
}
bool HttpCtx::hasHeader(const char* k) const {
  return httpd_req_get_hdr_value_len(_req, k) > 0;
}
String HttpCtx::header(const char* k) const {
  size_t len = httpd_req_get_hdr_value_len(_req, k);
  if (len == 0 || len > 1024) return String();
  char* buf = (char*)malloc(len + 1); if (!buf) return String();
  String out;
  if (httpd_req_get_hdr_value_str(_req, k, buf, len + 1) == ESP_OK) out = buf;
  free(buf); return out;
}

void HttpCtx::sendHeader(const char* k, const String& v) { _hdrs.push_back({ String(k), v }); }

static const char* statusStr(int code) {
  switch (code) { case 200: return "200 OK"; case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized"; case 404: return "404 Not Found";
    default: return "200 OK"; }
}
void HttpCtx::applyHead(int code, const char* ct) {
  httpd_resp_set_status(_req, statusStr(code));
  if (ct) httpd_resp_set_type(_req, ct);
  for (auto& h : _hdrs) httpd_resp_set_hdr(_req, h.first.c_str(), h.second.c_str());  // c_str は _hdrs 生存中有効
}

void HttpCtx::send(int code, const char* ct, const String& body) {
  applyHead(code, ct);
  if (body.length() > 0) { httpd_resp_send(_req, body.c_str(), body.length()); _state = 1; }
  else                   { _state = 2; }   // チャンク開始 (sendContent が続く)
}
void HttpCtx::send_P(int code, const char* ct, const uint8_t* data, size_t len) {
  applyHead(code, ct);
  httpd_resp_send(_req, (const char*)data, len);   // C3 は PROGMEM=通常メモリなので直接送出可
  _state = 1;
}
void HttpCtx::sendContent(const char* s, size_t n) {
  if (_state != 2 && _state != 0) return;
  if (n > 0) { httpd_resp_send_chunk(_req, s, n); _state = 2; }
  else       { httpd_resp_send_chunk(_req, NULL, 0); _state = 3; }   // 終端
}
void HttpCtx::sendContent(const char* s) { sendContent(s, s ? strlen(s) : 0); }

esp_err_t HttpCtx::finish() {
  if (_state == 2) httpd_resp_send_chunk(_req, NULL, 0);   // チャンク未終端 → 終端
  else if (_state == 0) { applyHead(200, "text/plain"); httpd_resp_send(_req, "", 0); }
  return ESP_OK;
}
