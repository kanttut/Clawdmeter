#include "screen_time.h"
#include <Arduino.h>
#include <Preferences.h>

static constexpr uint64_t day_ms = 86400000ULL;
static constexpr unsigned days = 8;
struct Bucket {
    uint32_t day;
    uint32_t ms;
};
struct StoredTime {
    uint64_t total;
    uint64_t undated;
    Bucket recent[days];
};
static StoredTime stored;
static uint64_t local_ms;
static uint32_t tick_ms;
static uint32_t saved_ms;
static bool was_lit;
static bool dirty;
static uint32_t unplugged_epoch;
static uint32_t unplugged_tick;
static bool unplug_pending;
static bool power_known;
static bool on_usb;

static void save_unplugged_epoch() {
    Preferences prefs;
    if (prefs.begin("screen-time", false)) {
        prefs.putUInt("unplug", unplugged_epoch);
        prefs.end();
    }
}

void screen_time_init() {
    Preferences prefs;
    if (prefs.begin("screen-time", true)) {
        unplugged_epoch = prefs.getUInt("unplug", 0);
        if (prefs.getBytesLength("v1") == sizeof(stored)) {
            prefs.getBytes("v1", &stored, sizeof(stored));
        }
        prefs.end();
    }
    tick_ms = saved_ms = millis();
}

static void record(uint32_t day, uint32_t ms) {
    auto& bucket = stored.recent[day % days];
    if (bucket.day != day) bucket = {day, 0};
    const uint32_t room = day_ms - bucket.ms;
    bucket.ms += ms < room ? ms : room;
}

void screen_time_tick(bool lit) {
    const uint32_t now = millis();
    const uint32_t elapsed = now - tick_ms;
    tick_ms = now;
    if (was_lit && elapsed) {
        stored.total += elapsed;
        dirty = true;
        if (local_ms) {
            const uint32_t today = local_ms / day_ms;
            const uint32_t until_midnight = day_ms - local_ms % day_ms;
            const uint32_t before_midnight = elapsed < until_midnight ? elapsed : until_midnight;
            record(today, before_midnight);
            if (elapsed > before_midnight) record(today + 1, elapsed - before_midnight);
        } else {
            stored.undated += elapsed;
        }
    }
    if (local_ms) local_ms += elapsed;
    was_lit = lit;
    if (!dirty || now - saved_ms < 60000) return;
    saved_ms = now;
    screen_time_flush();
}

void screen_time_flush() {
    if (!dirty) return;
    Preferences prefs;
    if (prefs.begin("screen-time", false)) {
        dirty = prefs.putBytes("v1", &stored, sizeof(stored)) != sizeof(stored);
        prefs.end();
    }
}

void screen_time_set_clock(uint32_t local_epoch) {
    if (local_epoch < 1700000000) return;
    local_ms = uint64_t(local_epoch) * 1000;
    if (unplug_pending) {
        unplugged_epoch = local_epoch - (millis() - unplugged_tick) / 1000;
        unplug_pending = false;
        save_unplugged_epoch();
    }
    if (stored.undated) {
        record(local_ms / day_ms, stored.undated < day_ms ? stored.undated : day_ms);
        stored.undated = 0;
        dirty = true;
    }
}

void screen_time_update_power(bool connected) {
    if (power_known && connected == on_usb) return;
    const bool unplugged = power_known && on_usb && !connected;
    power_known = true;
    on_usb = connected;
    if (unplugged) {
        unplugged_tick = millis();
        unplug_pending = local_ms == 0;
        if (!unplug_pending) {
            unplugged_epoch = local_ms / 1000;
            save_unplugged_epoch();
        }
    } else if (connected && (unplugged_epoch || unplug_pending)) {
        unplug_pending = false;
        unplugged_epoch = 0;
        save_unplugged_epoch();
    }
}

int32_t screen_time_since_charge() {
    if (on_usb) return -2;
    if (unplug_pending) return (millis() - unplugged_tick) / 1000;
    const uint32_t now = local_ms / 1000;
    return unplugged_epoch && now >= unplugged_epoch ? now - unplugged_epoch : -1;
}

ScreenTimeTotals screen_time_totals() {
    ScreenTimeTotals result = {0, 0, uint32_t(stored.total / 1000)};
    if (!local_ms) return result;
    const uint32_t today = local_ms / day_ms;
    uint64_t week = 0;
    for (const auto& bucket : stored.recent) {
        if (bucket.day == today) result.today = bucket.ms / 1000;
        if (bucket.day <= today && today - bucket.day < 7) week += bucket.ms;
    }
    result.week = week / 1000;
    return result;
}
