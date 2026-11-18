#pragma once
#include <Arduino.h>

#include <initializer_list>
#include <utility>

// Small HTTPS helper. Verifies servers against the embedded root CAs (root_certs.h), so the
// clock must be set (NTP) before the first request.
namespace net {
using Header = std::pair<const char*, String>;

struct Response {
  int status = 0;        // HTTP status, or < 0 for a transport error
  String body;
  String retryAfter;     // Retry-After header, if any
  String error() const;  // short human-readable error for status
};

Response request(const char* method, const String& url, const String& body,
                 std::initializer_list<Header> headers);

bool clockSet();
}
