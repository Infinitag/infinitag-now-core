// Firmware identity marker, embedded into every Infinitag-Now firmware.
//
// The config box stores device firmware images (OTA-Vollausbau, Doc 21)
// and needs to know WHICH device type and version a .bin contains. The
// IDF app descriptor is useless for that with prebuilt Arduino cores
// (it carries the framework version, not ours), so every firmware
// embeds this 13-byte marker instead:
//
//   '@' 'I' 'N' 'O' 'W' 'F' 'W' 0x01 <device_type> <major> <minor> <patch> '@'
//
// Usage (exactly once, in the firmware's main file):
//   INOW_FW_MARKER(inow::DEV_STATION, FW_MAJOR, FW_MINOR, FW_PATCH)
//
// Scanners must assemble the 8-byte prefix in two parts (see
// INOW_FW_MARKER_PREFIX_A/_B) so their own binary does not contain the
// contiguous pattern and match itself.

#pragma once

#include <stdint.h>

// 8-byte prefix, split so scanner binaries do not self-match.
#define INOW_FW_MARKER_PREFIX_A "@INO"
#define INOW_FW_MARKER_PREFIX_B "WFW\x01"
#define INOW_FW_MARKER_LEN 13

// Defined by INOW_FW_MARKER() in each firmware's main file. Referenced
// from EspNowService::begin() – that both keeps the array alive against
// linker --gc-sections (attribute((used)) only stops the compiler) and
// turns a missing marker into a link error instead of a silent gap.
extern "C" const uint8_t __inow_fw_marker[INOW_FW_MARKER_LEN];

#define INOW_FW_MARKER(devType, maj, min, pat)                          \
  extern "C" {                                                          \
  const uint8_t __inow_fw_marker[INOW_FW_MARKER_LEN]                    \
      __attribute__((used)) = {'@',       'I',   'N',   'O',   'W',    \
                               'F',       'W',   0x01,  (devType),     \
                               (maj),     (min), (pat), '@'};          \
  }
