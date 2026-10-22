// Unit tests for the captive-portal decision of the SoftAP update mode.
// Run on the PC:  pio test -e native

#include <unity.h>
#include "CaptivePortal.h"

using inow::captiveNeedsRedirect;

static const char *AP = "192.168.4.1";

void test_local_host_is_served() {
  TEST_ASSERT_FALSE(captiveNeedsRedirect("192.168.4.1", AP));
  TEST_ASSERT_FALSE(captiveNeedsRedirect("192.168.4.1:80", AP));
}

void test_missing_host_counts_as_local() {
  // HTTP/1.0 clients send no Host header - must not loop on the redirect
  TEST_ASSERT_FALSE(captiveNeedsRedirect("", AP));
  TEST_ASSERT_FALSE(captiveNeedsRedirect(nullptr, AP));
}

void test_os_probe_hosts_are_redirected() {
  TEST_ASSERT_TRUE(captiveNeedsRedirect("connectivitycheck.gstatic.com", AP));
  TEST_ASSERT_TRUE(captiveNeedsRedirect("captive.apple.com", AP));
  TEST_ASSERT_TRUE(captiveNeedsRedirect("www.msftconnecttest.com", AP));
  TEST_ASSERT_TRUE(captiveNeedsRedirect("example.com:8080", AP));
}

void test_prefix_lookalikes_are_redirected() {
  // same prefix, different address - must not pass as local
  TEST_ASSERT_TRUE(captiveNeedsRedirect("192.168.4.10", AP));
  TEST_ASSERT_TRUE(captiveNeedsRedirect("192.168.4.1x", AP));
  TEST_ASSERT_TRUE(captiveNeedsRedirect("192.168.4", AP));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_local_host_is_served);
  RUN_TEST(test_missing_host_counts_as_local);
  RUN_TEST(test_os_probe_hosts_are_redirected);
  RUN_TEST(test_prefix_lookalikes_are_redirected);
  return UNITY_END();
}
