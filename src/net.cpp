#include "net.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "root_certs.h"

namespace net {

bool clockSet() { return time(nullptr) > 1700000000; }

String Response::error() const {
  if (status < 0) return HTTPClient::errorToString(status);
  return "HTTP " + String(status);
}

Response request(const char* method, const String& url, const String& body,
                 std::initializer_list<Header> headers) {
  Response r;
  WiFiClientSecure client;
  client.setCACert(kRootCerts);
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(client, url)) {
    r.status = HTTPC_ERROR_CONNECTION_REFUSED;
    return r;
  }
  for (const auto& h : headers) http.addHeader(h.first, h.second);
  static const char* kCollect[] = {"Retry-After"};
  http.collectHeaders(kCollect, 1);

  r.status = http.sendRequest(method, body);
  if (r.status > 0) {
    r.body = http.getString();
    r.retryAfter = http.header("Retry-After");
  }
  http.end();
  return r;
}

}  // namespace net
