#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

#include "data.h"
#include "ui.h"
#include "ble.h"
#include "splash.h"
#include "charge_anim.h"
#include "usage_rate.h"
#include "idle.h"
#include "idle_cfg.h"
#include "brightness.h"
#include "wifi_sync.h"
#include "screen_time.h"
#ifdef BOARD_AMOLED_216_C6
#include "boards/waveshare_amoled_216_c6/board.h"
#include <esp_sleep.h>
#include <WiFi.h>
void board_display_sleep();

static void enter_board_sleep() {
    if (!idle_is_fully_asleep() || digitalRead(TP_INT) == LOW) return;
    if (esp_sleep_enable_ext1_wakeup_io(1ULL << TP_INT, ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) return;
    screen_time_flush();
    board_display_sleep();
    if (wifi_sync_configured()) WiFi.mode(WIFI_OFF);
    Serial.println("Deep sleep; tap screen to wake");
    Serial.flush();
    esp_deep_sleep_start();
}

static void track_limit_change(screen_t screen, const UsageData& data) {
    static bool seen[SCREEN_COUNT];
    static float session[SCREEN_COUNT];
    static float weekly[SCREEN_COUNT];
    if (!seen[screen] || session[screen] != data.session_pct || weekly[screen] != data.weekly_pct) {
        idle_note_limit_change();
    }
    seen[screen] = true;
    session[screen] = data.session_pct;
    weekly[screen] = data.weekly_pct;
}
#endif

#include "hal/board_caps.h"
#include "hal/display_hal.h"
#include "hal/touch_hal.h"
#include "hal/input_hal.h"
#include "hal/power_hal.h"
#include "hal/imu_hal.h"
#include "hal/sound_hal.h"

static UsageData usage = {};
static UsageData codex_usage = {};

// ---- LVGL draw buffers (partial render mode) ----
// PSRAM-equipped boards (S3) can comfortably hold larger strips. PSRAM-free
// boards (e.g. ESP32-C6) allocate from internal SRAM, so we shrink the strip
// — 480×20 RGB565 = 19 KB × 2 buffers = 38 KB, fits beside everything else.
#ifdef BOARD_HAS_PSRAM
#define BUF_LINES 40
#define LV_BUF_CAPS (MALLOC_CAP_SPIRAM)
#else
#define BUF_LINES 20
#define LV_BUF_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#endif
static uint16_t* buf1 = nullptr;
static uint16_t* buf2 = nullptr;

// Abstecher vom Splash auf die Auslastungszahlen: alle EVERY fuer SHOW lang.

static uint32_t my_tick(void) { return millis(); }

static void my_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    display_hal_draw_bitmap(area->x1, area->y1, w, h, (uint16_t*)px_map);
    // Letzter Streifen eines Durchlaufs: der Splash wartet darauf, bevor er
    // nach einem Screenwechsel wieder direkt auf den Panel malt.
    if (lv_display_flush_is_last(disp)) splash_note_refresh_done();
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    display_hal_round_area(&area->x1, &area->y1, &area->x2, &area->y2);
}

// Touch policy is driven by IDLE_WAKE_ON_TOUCH:
//   true  → a press edge while asleep wakes the device and the first touch is
//           swallowed (mirrors the button wake-consumption); a press while
//           awake counts as activity.
//   false → touch never counts as activity and is fully swallowed while the
//           panel is dark, so pets/sleeves can't wake it overnight and LVGL
//           can't quietly toggle splash<->usage on a black panel.
static void my_touch_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t x, y;
    bool pressed;
    touch_hal_read(&x, &y, &pressed);
    const bool raw_pressed = pressed;
    ui_note_touch(raw_pressed);

    if (IDLE_WAKE_ON_TOUCH) {
        static bool touch_was = false;
        static bool touch_wake_swallowed = false;
        if (raw_pressed && !touch_was) {
            if (!idle_is_asleep()) idle_note_activity();
            // Press edge — consume as wake if asleep.
            if (idle_consume_wake_press()) {
                touch_wake_swallowed = true;
                pressed = false;
            }
        } else if (!raw_pressed && touch_was) {
            // Release edge.
            if (touch_wake_swallowed) {
                touch_wake_swallowed = false;
                pressed = false;
            }
        } else if (raw_pressed && touch_wake_swallowed) {
            // Held finger through wake — keep hiding until release.
            pressed = false;
        }
        touch_was = raw_pressed;
    } else if (idle_is_asleep()) {
        pressed = false;
    }

    if (pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Parse a JSON line into UsageData.
static bool parse_json(const char* json, UsageData* out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return false;
    }

    out->session_pct = doc["s"] | 0.0f;
    out->session_reset_mins = doc["sr"] | -1;
    out->session_reset_secs = doc["ss"] | -1;
    out->weekly_pct = doc["w"] | 0.0f;
    out->weekly_reset_mins = doc["wr"] | -1;
    out->weekly_reset_secs = doc["ws"] | -1;
    strlcpy(out->session_reset_clock, doc["sa"] | "", sizeof(out->session_reset_clock));
    strlcpy(out->weekly_reset_clock, doc["wa"] | "", sizeof(out->weekly_reset_clock));
    strlcpy(out->status, doc["st"] | "unknown", sizeof(out->status));
    out->chime = doc["c"] | false;   // absent (old daemon / chime off) → stay silent
    const char* acct = doc["acct"] | "pro";
    out->enterprise = (strcmp(acct, "ent") == 0);
    out->time_pct = doc["tp"] | 0;
    out->period_days = doc["pd"] | 30;
    strlcpy(out->reset_date, doc["rd"] | "", sizeof(out->reset_date));
    strlcpy(out->anim, doc["a"] | "", sizeof(out->anim));
    out->clock_epoch = doc["t"] | 0L;
    out->clock_fmt = doc["tf"] | 24;
    out->ok = doc["ok"] | false;
    out->valid = true;
    JsonObjectConst codex = doc["codex"].as<JsonObjectConst>();
    codex_usage.valid = codex["s"].is<float>() && codex["w"].is<float>();
    if (codex_usage.valid) {
        codex_usage.session_pct = codex["s"].as<float>();
        codex_usage.weekly_pct = codex["w"].as<float>();
        codex_usage.session_reset_mins = codex["sr"] | -1;
        codex_usage.session_reset_secs = codex["ss"] | -1;
        codex_usage.weekly_reset_mins = codex["wr"] | -1;
        codex_usage.weekly_reset_secs = codex["ws"] | -1;
        strlcpy(codex_usage.session_reset_clock, codex["sa"] | "", sizeof(codex_usage.session_reset_clock));
        strlcpy(codex_usage.weekly_reset_clock, codex["wa"] | "", sizeof(codex_usage.weekly_reset_clock));
    }
    return true;
}

// ---- Serial command buffer ----
#ifdef BOARD_AMOLED_216_C6
#define CMD_BUF_SIZE 720
#else
#define CMD_BUF_SIZE 64
#endif
static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;
static bool cmd_overflow = false;

static void send_screenshot() {
#ifndef BOARD_HAS_PSRAM
    // A full RGB565 framebuffer doesn't fit in internal SRAM on PSRAM-free
    // boards (e.g. 480×480×2 = 460 KB). Capture is unsupported there.
    Serial.println("SCREENSHOT_UNSUPPORTED");
    return;
#else
    const uint32_t w = board_caps().width;
    const uint32_t h = board_caps().height;
    const uint32_t row_bytes = w * 2;
    const uint32_t buf_size = row_bytes * h;
    uint8_t* sbuf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!sbuf) {
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, w, h, LV_COLOR_FORMAT_RGB565, row_bytes, sbuf, buf_size);

    lv_result_t res = lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565, &draw_buf);
    if (res != LV_RESULT_OK) {
        heap_caps_free(sbuf);
        Serial.println("SCREENSHOT_ERR");
        return;
    }

    Serial.printf("SCREENSHOT_START %lu %lu %lu\n",
        (unsigned long)w, (unsigned long)h, (unsigned long)buf_size);
    Serial.flush();
    Serial.write(sbuf, buf_size);
    Serial.flush();
    Serial.println();
    Serial.println("SCREENSHOT_END");
    heap_caps_free(sbuf);
#endif
}

static void check_serial_cmd() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmd_overflow) {
                cmd_overflow = false;
                cmd_pos = 0;
                continue;
            }
            cmd_buf[cmd_pos] = '\0';
#ifdef BOARD_AMOLED_216_C6
            if (strncmp(cmd_buf, "wifi-config ", 12) == 0) wifi_sync_configure(cmd_buf + 12);
            else if (strncmp(cmd_buf, "wifi-key ", 9) == 0) wifi_sync_replace_key(cmd_buf + 9);
            else if (strcmp(cmd_buf, "wifi-status") == 0) wifi_sync_status();
            else
#endif
            if (strcmp(cmd_buf, "screenshot") == 0) send_screenshot();
            else if (strcmp(cmd_buf, "buzz") == 0)  sound_hal_play_reset();
            // Play the charge overlay without touching the cable — the real
            // trigger needs a USB transition, which is awkward to produce on a
            // device that is being flashed over that same cable.
            else if (strcmp(cmd_buf, "charge") == 0)   charge_anim_play(true);
            else if (strcmp(cmd_buf, "uncharge") == 0) charge_anim_play(false);
            cmd_pos = 0;
        } else if (!cmd_overflow && cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
        } else {
            cmd_overflow = true;
        }
    }
}

// Each board provides this. Must bring up the shared I2C bus (Wire.begin
// with the board's SDA/SCL pins) and any board-private hardware that has
// to settle before display/touch (e.g. an IO expander gating the LCD
// reset line). Called exactly once at the start of setup().
extern "C" void board_init(void);

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("{\"ready\":true}");

    board_init();

    display_hal_init();
    display_hal_begin();
    idle_init();        // takes over panel brightness and starts the idle timer
    brightness_init();  // load the user's saved brightness level and apply via idle

    power_hal_init();
    imu_hal_init();
    sound_hal_init();
    touch_hal_init();

    // ---- LVGL ----
    const int W = board_caps().width;
    const int H = board_caps().height;

    lv_init();
    lv_tick_set_cb(my_tick);

    buf1 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);
    buf2 = (uint16_t*)heap_caps_malloc(W * BUF_LINES * 2, LV_BUF_CAPS);

    lv_display_t* disp = lv_display_create(W, H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, my_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, W * BUF_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touch_cb);

#ifdef BOARD_AMOLED_216_C6
    wifi_sync_init();
    if (!wifi_sync_configured())
#endif
        ble_init();
    input_hal_init();

    ui_init();
    screen_time_init();
    Preferences notes;
    if (notes.begin("notes", true)) {
        ui_update_note(notes.getString("current").c_str(), notes.getUInt("checks", notes.getBool("done")));
        notes.end();
    }
    ui_update_ble_status(ble_get_state(), ble_get_device_name(), ble_get_mac_address());
    ui_update_battery(power_hal_battery_pct(), power_hal_is_charging());
    ui_show_screen(SCREEN_USAGE);

    Serial.printf("Dashboard ready (%s, %dx%d), waiting for data on BLE...\n",
        board_caps().name, W, H);
}

static ble_state_t last_ble_state = BLE_STATE_INIT;

// Hold-to-pair gesture: hold the PWR button ~3s, then RELEASE → clear all BLE
// bonds and re-advertise. Clearing on *release* (not while held) is deliberate:
// holding to power the device OFF (AXP hardware shutdown at 8s) must not wipe
// the bond — a power-off hold never releases before shutdown. To stop a
// "chicken-out" release just before 8s from pairing, the gesture disarms at 6s.
//
//   ~1.5s long-press edge → PENDING
//   3.0s (+1500)          → ARMED   (release from here clears bonds)
//   6.0s (+4500)          → DISARMED (no clear; AXP powers off at 8s)
#define PAIR_ARM_AFTER_LONG_MS    1500   // 3.0s total
#define PAIR_DISARM_AFTER_LONG_MS 4500   // 6.0s total
enum pair_state_t { PAIR_IDLE, PAIR_PENDING, PAIR_ARMED };
static pair_state_t pair_state        = PAIR_IDLE;
static uint32_t     pair_long_seen_ms = 0;

static void pair_tick(void) {
    if (pair_state == PAIR_IDLE && power_hal_pwr_long_pressed()) {
        pair_state = PAIR_PENDING;
        pair_long_seen_ms = millis();
        (void)power_hal_pwr_released();  // drain any stale release edge
        Serial.println("PWR long-press: hold to ~3s then release to pair");
        return;
    }
    if (pair_state == PAIR_IDLE) return;

    if (power_hal_pwr_released()) {
        if (pair_state == PAIR_ARMED) {
            Serial.println("Pair: released in window — clearing bonds, advertising");
            ble_clear_bonds();
        } else {
            Serial.println("Pair: released too early — cancelled");
        }
        pair_state = PAIR_IDLE;
        return;
    }

    uint32_t held = millis() - pair_long_seen_ms;
    if (pair_state == PAIR_PENDING && held >= PAIR_ARM_AFTER_LONG_MS) {
        pair_state = PAIR_ARMED;
        Serial.println("Pair: armed — release to pair");
    } else if (pair_state == PAIR_ARMED && held >= PAIR_DISARM_AFTER_LONG_MS) {
        pair_state = PAIR_IDLE;  // power-off territory; don't pair
        Serial.println("Pair: disarmed (holding toward power-off)");
    }
}

void loop() {
    idle_tick();
    screen_time_tick(idle_is_display_lit());
    lv_timer_handler();
    ui_tick_anim();
#ifdef BOARD_AMOLED_216_C6
    if (!wifi_sync_configured())
#endif
        ble_tick();
    power_hal_tick();
    imu_hal_tick();
    sound_hal_tick();
    splash_tick();
    // Rotation transition (blank + ramp) would fight the idle fade — skip
    // ticks while the panel is dark. A rotation that happens during sleep
    // is detected by the next tick after wake and ramped in then.
    if (!idle_is_asleep()) display_hal_tick();

    // ---- Physical buttons ----
    //   PRIMARY   → toggle the animated splash
    //   SECONDARY → Claude, Codex
    //   PWR       → cycle brightness;
    //               hold ~3s + release: pairing mode
    // First press from sleep is consumed as a wake-only event by
    // idle_consume_wake_press(); the normal action fires from the second
    // press. Activity bookkeeping happens inside idle_consume_wake_press
    // so no separate idle_note_activity() call is needed here.
    {
        static bool primary_was = false;
        static bool primary_consumed = false;
        static uint32_t primary_started = 0;
        static uint32_t last_press;
        static unsigned presses;
        bool primary_now = input_hal_is_held(INPUT_BTN_PRIMARY);
        const uint32_t now = millis();
        if (primary_now && !primary_was) {
            primary_started = now;
            primary_consumed = idle_consume_wake_press();
            if (primary_consumed) presses = 0;
        } else if (primary_now && !primary_consumed && now - primary_started >= 3000) {
            primary_consumed = true;
            presses = 0;
            idle_sleep_now();
        } else if (!primary_now && primary_was && !primary_consumed) {
            if (now - last_press > 950) presses = 0;
            last_press = now;
            if (++presses == 5) {
                presses = 0;
                ui_start_ball_game();
            } else {
                ui_toggle_splash();
            }
        }
        primary_was = primary_now;

        if (board_caps().button_count >= 2) {
            static bool secondary_was = false;
            bool secondary_now = input_hal_is_held(INPUT_BTN_SECONDARY);
            if (secondary_now != secondary_was) {
                if (secondary_now && !idle_consume_wake_press()) {
                    auto screen = static_cast<screen_t>((ui_get_current_screen() + 1) % SCREEN_COUNT);
                    ui_show_screen(screen);
                    const char* names[] = {"Claude", "Codex", "Notes"};
                    Serial.printf("KEY: %s\n", names[screen]);
                }
                secondary_was = secondary_now;
            }
        }

        if (power_hal_pwr_pressed()) {
            if (!idle_consume_wake_press()) {
                brightness_cycle();
            }
        }

#ifdef BOARD_AMOLED_216_C6
        if (!wifi_sync_configured())
#endif
            pair_tick();
    }

    ble_state_t bs = ble_get_state();
#ifdef BOARD_AMOLED_216_C6
    if (wifi_sync_configured()) {
        bs = wifi_sync_online() ? BLE_STATE_CONNECTED : BLE_STATE_INIT;
    }
#endif
    if (bs != last_ble_state) {
        last_ble_state = bs;
        ui_update_ble_status(bs, ble_get_device_name(), ble_get_mac_address());
    }

    static int  last_pct      = -2;
    static bool last_charging = false;
    int  pct      = power_hal_battery_pct();
    bool charging = power_hal_is_charging();
    if (pct != last_pct || charging != last_charging) {
        if (pct != last_pct) ble_set_battery_level(pct);
        last_pct = pct;
        last_charging = charging;
        ui_update_battery(pct, charging);
    }

    // Cable in / out gets a short animation. Driven by VBUS rather than the
    // charging flag, which also drops when the battery reaches full with the
    // cable still in — that would play the unplug sequence for nothing.
    // The first reading only records the state: booting on USB is not an event.
    static bool vbus_known = false;
    static bool last_vbus  = false;
    bool vbus = power_hal_is_vbus_in();
    screen_time_update_power(vbus);
    if (!vbus_known) {
        vbus_known = true;
        last_vbus  = vbus;
    } else if (vbus != last_vbus) {
        last_vbus = vbus;
        Serial.printf("USB %s\n", vbus ? "in" : "out");
        charge_anim_play(vbus);
    }

    check_serial_cmd();

    const char* packet = nullptr;
    bool via_wifi = false;
#ifdef BOARD_AMOLED_216_C6
    via_wifi = wifi_sync_configured();
    if (via_wifi) packet = wifi_sync_receive();
#endif
    if (!via_wifi && ble_has_data()) packet = ble_get_data();
    if (via_wifi && packet) {
        JsonDocument message;
        const bool decoded = !deserializeJson(message, packet);
        if (decoded && message["localTime"].is<uint32_t>()) {
            screen_time_set_clock(message["localTime"].as<uint32_t>());
        }
#ifdef BOARD_AMOLED_216_C6
        if (decoded && message["control"] == true) {
            const int page = message["page"] | -1;
            if (page >= 0 && page < SCREEN_COUNT) ui_show_screen(static_cast<screen_t>(page));
            const int level = message["brightness"] | -1;
            if (level >= 0 && level < 4) brightness_set_step(level);
            if (message["interval"].is<unsigned>()) ui_set_page_interval(message["interval"].as<unsigned>());
            if (message["pin"].is<bool>()) ui_set_pinned(message["pin"].as<bool>());
            if (message["mode"] == "game") ui_start_ball_game();
            if (message["mode"] == "splash" && strcmp(ui_get_mode(), "splash")) {
                if (!strcmp(ui_get_mode(), "game")) ui_show_screen(ui_get_current_screen());
                ui_toggle_splash();
            }
            const auto duration = screen_time_totals();
            char status[320];
            snprintf(status, sizeof(status),
                "{\"page\":%d,\"brightness\":%u,\"mode\":\"%s\",\"interval\":%u,\"pinned\":%s,"
                "\"checks\":%lu,\"claudeAge\":%d,\"codexAge\":%d,"
                "\"screenToday\":%lu,\"screenWeek\":%lu,\"screenTotal\":%lu,\"displayLit\":%s,\"chargeAge\":%ld}",
                ui_get_current_screen(), brightness_get(), ui_get_mode(), ui_get_page_interval(),
                ui_is_pinned() ? "true" : "false", static_cast<unsigned long>(ui_get_note_checks()),
                ui_get_usage_age(SCREEN_USAGE), ui_get_usage_age(SCREEN_CODEX),
                static_cast<unsigned long>(duration.today), static_cast<unsigned long>(duration.week),
                static_cast<unsigned long>(duration.total), idle_is_display_lit() ? "true" : "false",
                static_cast<long>(screen_time_since_charge()));
            wifi_sync_ack(status);
            packet = nullptr;
        } else
#endif
        if (decoded && message["note"].is<const char*>()) {
            const char* note = message["note"];
            Preferences notes;
            if (notes.begin("notes", false)) {
                const bool unchanged = notes.getString("current") == note;
                if (unchanged || notes.putString("current", note)) {
                    if (!unchanged) notes.putUInt("checks", 0);
                    if (!unchanged) ui_update_note(note);
#ifdef BOARD_AMOLED_216_C6
                    wifi_sync_ack();
#endif
                    Serial.println("Note saved");
                }
                notes.end();
            }
            packet = nullptr;
        }
    }
    if (packet) {
        if (parse_json(packet, &usage)) {
            if (!via_wifi || usage.ok) {
                bool session_reset = usage_rate_sample(usage.session_pct);
                splash_set_anim(usage.anim);
                if (session_reset && usage.chime) {
                    Serial.println("session reset detected — chime");
                    sound_hal_play_reset();
                }
                ui_update(&usage);
#ifdef BOARD_AMOLED_216_C6
                track_limit_change(SCREEN_USAGE, usage);
#endif
            }
            if (codex_usage.valid) {
                ui_update(&codex_usage, SCREEN_CODEX);
#ifdef BOARD_AMOLED_216_C6
                track_limit_change(SCREEN_CODEX, codex_usage);
#endif
                Serial.println("Codex limits updated");
            }
            if (!via_wifi) ble_send_ack();
#ifdef BOARD_AMOLED_216_C6
            else wifi_sync_ack();
#endif
        } else {
            if (!via_wifi) ble_send_nack();
        }
    }

#ifdef BOARD_AMOLED_216_C6
    enter_board_sleep();
#endif
    delay(5);
}
