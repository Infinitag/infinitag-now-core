// Central sound catalog for the Infinitag-Now system.
//
// The sound_id used on the radio (HIT_REPORT, CFG_TEST_SOUND, target
// config) is the INDEX into this table. The station resolves the index
// to a LittleFS file path; the config box shows the short name next to
// the id so nobody has to remember numbers.
//
// Adding/changing sounds: edit this table (see data/README.md in the
// station repo for the full guide), reflash BOTH station and config box
// so ids, files and names stay in sync, and upload the matching WAV
// files to the station's LittleFS (pio run -t uploadfs).
//
// Names are display strings for the 128x64 OLEDs – keep them <= 8
// characters so every list row fits.

#pragma once

#include <stdint.h>

namespace inow {

struct SoundInfo {
  const char *file;  // path in the station's LittleFS
  const char *name;  // short display name (<= 8 chars)
};

static constexpr SoundInfo SOUND_CATALOG[] = {
    {"/01_test.wav", "Test"},
    {"/02_door_bang.wav", "DoorBang"},
    {"/03_boo_and_laugh.wav", "BooLaugh"},
    {"/04_bubbles.wav", "Bubbles"},
    {"/05_cat_meow.wav", "CatMeow"},
    {"/06_daemon_kinderliebe.wav", "Daemon"},
    {"/07_gears.wav", "Gears"},
    {"/08_little_girl.wav", "Girl"},
    {"/09_owl_hooting.wav", "Owl"},
    {"/10_psycho_sound.wav", "Psycho"},
    {"/11_scary_clock.wav", "Clock"},
    {"/12_spooky_skeleton.wav", "Skeleton"},
    {"/13_werewolf.wav", "Werewolf"},
    {"/14_werewolf_growl.wav", "WwGrowl"},
    {"/15_witch.wav", "Witch"},
};

static constexpr uint8_t SOUND_COUNT =
    sizeof(SOUND_CATALOG) / sizeof(SOUND_CATALOG[0]);

// Display name for a sound id; "?" for ids outside the catalog.
inline const char *soundName(uint8_t id) {
  return id < SOUND_COUNT ? SOUND_CATALOG[id].name : "?";
}

}  // namespace inow
