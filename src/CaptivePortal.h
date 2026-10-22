// Captive-portal decision helper for the SoftAP update mode.
//
// Pure, Arduino-free (natively unit-tested): WebUpdateService answers
// every DNS query with its own IP, so the phone's connectivity probe
// (Android /generate_204, iOS /hotspot-detect.html, Windows
// /connecttest.txt, ...) arrives here with a FOREIGN Host header. Those
// requests get a 302 to http://<apIp>/ - that is what makes the OS pop
// up the "sign in to network" page. Requests addressed to the AP IP
// itself are served normally.

#pragma once
#include <stddef.h>
#include <string.h>

namespace inow {

// true = the request carries a Host header that is NOT the AP IP (with or
// without ":port") and must be redirected. An empty/missing Host header
// counts as local: HTTP/1.0 clients omit it and would otherwise loop.
inline bool captiveNeedsRedirect(const char *host, const char *apIp) {
  if (!host || !*host || !apIp || !*apIp) return false;
  const size_t n = strlen(apIp);
  if (strncmp(host, apIp, n) != 0) return true;
  return !(host[n] == '\0' || host[n] == ':');
}

}  // namespace inow
