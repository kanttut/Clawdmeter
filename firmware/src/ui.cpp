#include "ui.h"
#include "splash.h"
#include "charge_anim.h"
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>
#include "logo.h"
#include "chatgpt_logo.h"
#include "waiting_message.h"
#include "wifi_sync.h"
#include "idle.h"
#include "icons.h"
#include "hal/board_caps.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_styrene_12);
LV_FONT_DECLARE(font_mono_32);
LV_FONT_DECLARE(font_mono_18);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;
    int16_t bar_h;
    int16_t panel_pad_x, panel_pad_y;
    int16_t pill_pad_x, pill_pad_y;
    const lv_font_t* title_font;     // screen title / clock
    const lv_font_t* pct_font;       // big percentage number
    const lv_font_t* ent_pct_font;   // enterprise spending number
    const lv_font_t* pill_font;      // "Current" / "Weekly" pill
    const lv_font_t* reset_font;     // "Resets in ..." line
    const lv_font_t* pace_font;      // enterprise "Under/On/Over pace" line
    const lv_font_t* anim_font;      // animated status line
    int16_t anim_y;                  // status line offset from bottom
    bool    small_icons;             // 40px logo + 24px battery (vs 80/48) on small screens
    int16_t title_nudge;             // title x-shift balancing the corner logo
    int16_t logo_y;                  // logo top edge
    int16_t batt_y;                  // battery icon top edge
    int16_t batt_w;                  // battery icon width, for position math

    // Pairing hint / idle screen
    int16_t pair_y1, pair_y2, pair_y3;
    int16_t idle_px;                 // sleeping-creature size on the idle screen

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    // Values shared by the two original breakpoints; the small branch below
    // overrides them wholesale.
    L.bar_h = 24;
    L.panel_pad_x = 16;
    L.panel_pad_y = 12;
    L.pill_pad_x = 18;
    L.pill_pad_y = 6;
    L.title_font   = &font_tiempos_56;
    L.pct_font     = &font_styrene_48;
    L.ent_pct_font = &font_tiempos_56;
    L.pill_font    = &font_styrene_28;
    L.reset_font   = &font_styrene_28;
    L.pace_font    = &font_styrene_16;
    L.anim_font    = &font_mono_32;
    L.anim_y = -15;
    L.small_icons = false;
    L.title_nudge = 16;
    L.logo_y = L.title_y - 10;
    L.batt_y = L.title_y;
    L.batt_w = ICON_BATTERY_W;
    L.pair_y1 = 40;
    L.pair_y2 = 120;
    L.pair_y3 = 160;
    L.idle_px = 160;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else if (c.height >= 300) {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    } else {
        // Small layout — tuned for 240x240 (LCD-1.54 and similar square TFTs).
        // Everything shrinks: fonts two steps down, panels ~half height, and
        // the corner logo/battery switch to the 40px/24px small assets.
        L.margin = 8;
        L.title_y = 4;
        L.content_y = 44;
        L.usage_panel_h = 74;
        L.usage_panel_gap = 6;
        L.usage_bar_y = 30;
        L.usage_reset_y = 46;
        L.bar_h = 12;
        L.panel_pad_x = 10;
        L.panel_pad_y = 6;
        L.pill_pad_x = 8;
        L.pill_pad_y = 2;
        L.title_font   = &font_tiempos_34;
        L.pct_font     = &font_styrene_24;
        L.ent_pct_font = &font_tiempos_34;
        L.pill_font    = &font_styrene_14;
        L.reset_font   = &font_styrene_14;
        L.pace_font    = &font_styrene_12;
        L.anim_font    = &font_mono_18;
        // Center the status line in the strip below the weekly panel; flush
        // against the bottom edge it reads as unevenly spaced.
        L.anim_y = -10;
        L.small_icons = true;
        L.title_nudge = 8;
        L.logo_y = 2;
        L.batt_y = 10;
        L.batt_w = ICON_BATTERY_SMALL_W;
        L.pair_y1 = 12;
        L.pair_y2 = 56;
        L.pair_y3 = 80;
        L.idle_px = 96;
        L.bt_info_panel_h = 90;
        L.bt_reset_zone_h = 60;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_20;
        L.bt_device_font   = &font_styrene_14;
        L.bt_credit_1_font = &font_styrene_12;
        L.bt_credit_2_font = &font_styrene_12;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets (single non-splash view) ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
// Clock fed by the daemon: base epoch (local wall-clock seconds) + the lv_tick at
// which it landed, so the title ticks forward locally between 60s payloads.
static long     clock_base_epoch = 0;
static uint32_t clock_base_ms = 0;
static int      clock_fmt = 24;   // 12 or 24, set from the daemon payload
static int      clock_last_min = -1;   // last rendered minute; avoids redrawing the title every tick
static lv_obj_t* usage_group;   // the two usage panels — shown when connected
static lv_obj_t* pair_group;    // pairing hint — shown when disconnected
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* lbl_session_reset_clock;
static lv_obj_t* lbl_weekly_reset_clock;
static lv_obj_t* notes_page;
static lv_obj_t* note_card;
static constexpr size_t max_note_items = 12;
static lv_obj_t* note_rows[max_note_items];
static size_t note_count;
static uint32_t note_checks;
LV_FONT_DECLARE(notes_font);
static lv_obj_t* lbl_limit_timer;
static lv_obj_t* bounce_canvas;
alignas(4) static uint16_t bounce_pixels[48][112];
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* panel_session = nullptr;
static lv_obj_t* panel_weekly = nullptr;
// Enterprise-only widgets inside panel_session
static lv_obj_t* lbl_session_pct_sym = nullptr;  // "%" in smaller font
static lv_obj_t* lbl_spending_desc = nullptr;     // "of your monthly budget"
static lv_obj_t* lbl_spending_status = nullptr;   // "Under pace" / "On pace" / "Over pace"
static lv_obj_t* lbl_anim;      // status line: connection state + whimsical idle

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* battery_percent_label;
static uint32_t battery_tapped_ms;
static lv_obj_t* logo_img;
static lv_obj_t* idle_claude_img;
static lv_obj_t* idle_codex_img;
static lv_obj_t* idle_ball;
static lv_obj_t* game_label;
static lv_obj_t* game_instruction;
static lv_obj_t* game_target;
static lv_obj_t* wait_message;
static bool ball_game;
static bool ball_tapped;
static uint32_t game_started_ms;
static unsigned game_hits;
static unsigned game_streak;
static unsigned game_serve;
static uint32_t game_last_frame;
static int game_contact_x;
static int game_contact_y;
enum class GamePhase { Ready, Serve, Return, Miss };
static GamePhase game_phase;
static lv_image_dsc_t waiting_message_dsc;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Live-data freshness → which usage sub-view to show ----
// usage panels when data is flowing, an idle "Zzz" screen when the host is
// connected but no usage update landed within DATA_FRESH_MS, the pairing hint
// when BLE is down. Re-evaluated every loop in ui_tick_anim().
static lv_obj_t* idle_group;            // the "Zzz" idle screen
static uint32_t last_data_ms[SCREEN_COUNT] = {};
static UsageData usage_by_screen[SCREEN_COUNT] = {};
static int       view_state = -1;       // -1 unknown / 0 pair / 1 idle / 2 usage
static const uint32_t DATA_FRESH_MS = 90000;  // usage counts as "live" within this window (daemon sends ~60s)

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static lv_image_dsc_t chatgpt_logo_dsc;
static screen_t current_screen = SCREEN_USAGE;
static uint32_t page_started_ms;
static uint32_t page_interval_ms = 5000;
static uint32_t page_delay_ms = 5000;

static bool page_pinned;
static bool showing_splash = false;
static bool demo_active;
static unsigned demo_index;
static screen_t demo_return_screen;
static bool demo_return_pin;
static UsageData demo_usage;
static uint32_t demo_started_ms;
static constexpr int demo_start_minutes = 12 * 60;

bool ui_is_demo() { return demo_active; }

void ui_set_pinned(bool pinned) {
    page_pinned = pinned;
    page_started_ms = lv_tick_get();
    page_delay_ms = page_interval_ms;
}

void ui_set_page_interval(unsigned seconds) {
    if (seconds < 3 || seconds > 60) return;
    page_interval_ms = seconds * 1000;
    page_delay_ms = page_interval_ms;
    page_started_ms = lv_tick_get();
}

bool ui_is_pinned() { return page_pinned; }
unsigned ui_get_page_interval() { return page_interval_ms / 1000; }
const char* ui_get_mode() {
    if (demo_active) return "demo";
    return ball_game ? "game" : showing_splash ? "splash" : "page";
}
uint32_t ui_get_note_checks() { return note_checks; }
int ui_get_usage_age(screen_t screen) {
    return usage_by_screen[screen].valid ? int((lv_tick_get() - last_data_ms[screen]) / 1000) : -1;
}

void ui_note_touch(bool pressed) {
    static bool held;
    static uint32_t touch_started;
    const uint32_t now = lv_tick_get();
    if (pressed) {
        if (!held) {
            touch_started = now;
            if (ball_game) ball_tapped = true;
        }
        if (now - touch_started >= 700) page_pinned = true;
        page_started_ms = now;
        page_delay_ms = 15000;
    }
    held = pressed;
}
static bool     s_ble_connected = false;   // cached BLE connection state
static uint32_t connected_at_ms = 0;       // when we last entered CONNECTED ("Connected" dwell)

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, mins % 1440 / 60);
    }
}

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_right(panel, L.panel_pad_x, 0);
    lv_obj_set_style_pad_top(panel, L.panel_pad_y, 0);
    lv_obj_set_style_pad_bottom(panel, L.panel_pad_y, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, L.pill_font, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_right(lbl, L.pill_pad_x, 0);
    lv_obj_set_style_pad_top(lbl, L.pill_pad_y, 0);
    lv_obj_set_style_pad_bottom(lbl, L.pill_pad_y, 0);
    return lbl;
}

static void init_battery_icons(void) {
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_SMALL_W, ICON_BATTERY_SMALL_H, icon_battery_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_SMALL_W, ICON_BATTERY_LOW_SMALL_H, icon_battery_low_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_SMALL_W, ICON_BATTERY_MEDIUM_SMALL_H, icon_battery_medium_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_SMALL_W, ICON_BATTERY_FULL_SMALL_H, icon_battery_full_small_data);
        init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_SMALL_W, ICON_BATTERY_CHARGING_SMALL_H, icon_battery_charging_small_data);
        return;
    }
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static lv_obj_t* make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                                  lv_obj_t** out_pct, lv_obj_t** out_pill,
                                  lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, L.pct_font, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y,
                        L.content_w - 2 * L.panel_pad_x, L.bar_h);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, L.reset_font, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);

    return panel;
}

static lv_obj_t* make_reset_clock(lv_obj_t* panel) {
    lv_obj_t* label = lv_label_create(panel);
    lv_label_set_text_static(label, "");
    lv_obj_set_style_text_font(label, L.reset_font, 0);
    lv_obj_set_style_text_color(label, COL_TEXT, 0);
    lv_obj_align(label, LV_ALIGN_TOP_RIGHT, 0, L.usage_reset_y);
    return label;
}

static void reorder_notes() {
    for (unsigned checked = 0; checked < 2; ++checked) {
        for (size_t index = 0; index < note_count; ++index) {
            if (((note_checks >> index) & 1) == checked) lv_obj_move_to_index(note_rows[index], -1);
        }
    }
}

static void note_check_changed(lv_event_t* event) {
    auto* checkbox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    const bool done = lv_obj_has_state(checkbox, LV_STATE_CHECKED);
    const uint32_t bit = uint32_t{1} << index;
    note_checks = done ? note_checks | bit : note_checks & ~bit;
    auto* label = lv_obj_get_child(note_rows[index], 1);
    lv_obj_set_style_text_decor(label, done ? LV_TEXT_DECOR_STRIKETHROUGH : LV_TEXT_DECOR_NONE, 0);
    reorder_notes();
    Preferences prefs;
    if (prefs.begin("notes", false)) {
        prefs.putUInt("checks", note_checks);
        prefs.end();
    }
}

static void build_notes_page(lv_obj_t* parent) {
    notes_page = lv_obj_create(parent);
    lv_obj_remove_style_all(notes_page);
    lv_obj_set_size(notes_page, L.scr_w, L.scr_h);
    lv_obj_clear_flag(notes_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_hidden(notes_page, true);

    lv_obj_t* title = lv_label_create(notes_page);
    lv_label_set_text_static(title, "Заметки");
    lv_obj_set_style_text_font(title, &notes_font, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, L.title_y);

    note_card = make_panel(notes_page, L.margin, L.content_y,
                           L.content_w, L.scr_h - L.content_y - L.margin);
    lv_obj_add_flag(note_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(note_card, LV_DIR_VER);
    lv_obj_set_flex_flow(note_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(note_card, 12, 0);
    ui_update_note("");
}

static void update_note_checkbox_hit_area(lv_event_t* event) {
    auto* checkbox = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const int32_t width = lv_obj_get_width(checkbox);
    const int32_t height = lv_obj_get_height(checkbox);
    // For a nearly square checkbox, add half of (sqrt(1.4) - 1) per edge.
    const int32_t padding = ((width + height) * 46 + 500) / 1000;
    lv_obj_set_ext_click_area(checkbox, padding);

    // Let input reach the checkbox even where its hit area crosses the row edge.
    auto* row = lv_obj_get_parent(checkbox);
    lv_obj_set_ext_draw_size(row, padding);
    lv_obj_refresh_ext_draw_size(row);
}

void ui_update_note(const char* text, uint32_t done) {
    lv_obj_clean(note_card);
    note_count = 0;
    note_checks = done;
    const int note_x = lv_font_get_line_height(LV_FONT_DEFAULT) + 34;
    const int width = L.content_w - 2 * L.panel_pad_x;
    while (*text && note_count < max_note_items) {
        const char* end = strchr(text, '\n');
        const size_t length = end ? end - text : strlen(text);
        String item(text, length);
        item.trim();
        text += length + (end ? 1 : 0);
        if (!item.length()) continue;
        const size_t index = note_count++;
        auto* row = lv_obj_create(note_card);
        note_rows[index] = row;
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, width, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(row, lv_font_get_line_height(LV_FONT_DEFAULT) + 20, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        auto* checkbox = lv_checkbox_create(row);
        lv_checkbox_set_text_static(checkbox, "");
        lv_obj_set_style_pad_all(checkbox, 10, LV_PART_INDICATOR);
        lv_obj_set_style_border_width(checkbox, 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(checkbox, COL_PANEL, LV_PART_INDICATOR);
        lv_obj_set_style_border_color(checkbox, COL_DIM, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(checkbox, COL_PANEL, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_recolor(checkbox, lv_color_hex(0x58D97C), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_image_recolor_opa(checkbox, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(checkbox, update_note_checkbox_hit_area, LV_EVENT_SIZE_CHANGED, nullptr);
        lv_obj_add_event_cb(checkbox, note_check_changed, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(index));
        auto* label = lv_label_create(row);
        lv_obj_set_pos(label, note_x, 0);
        lv_obj_set_width(label, width - note_x);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, item.c_str());
        lv_obj_set_style_text_font(label, &notes_font, 0);
        lv_obj_set_style_text_color(label, COL_TEXT, 0);
        if ((note_checks >> index) & 1) {
            lv_obj_add_state(checkbox, LV_STATE_CHECKED);
            lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        }
    }
    if (!note_count) {
        auto* label = lv_label_create(note_card);
        lv_obj_set_style_text_font(label, &notes_font, 0);
        lv_obj_set_style_text_color(label, COL_DIM, 0);
        lv_label_set_text_static(label, "Пока нет заметок");
    }
    reorder_notes();
    lv_obj_scroll_to_y(note_card, 0, LV_ANIM_OFF);
}

// Pairing hint — shown when disconnected so the screen isn't empty and the
// user knows how to (re)pair. Wording matches the 3-second release gesture.
static void build_pair_group(lv_obj_t* parent) {
    pair_group = lv_obj_create(parent);
    lv_obj_set_size(pair_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(pair_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(pair_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pair_group, 0, 0);
    lv_obj_set_style_pad_all(pair_group, 0, 0);
    lv_obj_clear_flag(pair_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* l1 = lv_label_create(pair_group);
    lv_label_set_text(l1, "To pair");
    lv_obj_set_style_text_font(l1, L.bt_status_font, 0);
    lv_obj_set_style_text_color(l1, COL_TEXT, 0);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, L.pair_y1);

    lv_obj_t* l2 = lv_label_create(pair_group);
    lv_label_set_text(l2, "hold the power button");
    lv_obj_set_style_text_font(l2, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l2, COL_DIM, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, L.pair_y2);

    lv_obj_t* l3 = lv_label_create(pair_group);
    lv_label_set_text(l3, "for 3 seconds, then release");
    lv_obj_set_style_text_font(l3, L.bt_device_font, 0);
    lv_obj_set_style_text_color(l3, COL_DIM, 0);
    lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, L.pair_y3);
#ifdef BOARD_AMOLED_216_C6
    if (wifi_sync_configured()) {
        lv_label_set_text_static(l1, "Wi-Fi");
        lv_label_set_text_static(l2, "Connecting to network");
        lv_label_set_text_static(l3, "");
    }
#endif

    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);  // ui_update_ble_status decides
}

// Idle "Zzz" screen — shown when the host is connected but no usage update has
// landed recently (token expired, daemon down, host asleep…). Full-screen, like
// the pairing hint, so we never render hours-old numbers as if they were live.
static void build_idle_group(lv_obj_t* parent) {
    idle_group = lv_obj_create(parent);
    lv_obj_set_size(idle_group, L.scr_w, L.scr_h - L.content_y);
    lv_obj_set_pos(idle_group, 0, L.content_y);
    lv_obj_set_style_bg_opa(idle_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(idle_group, 0, 0);
    lv_obj_set_style_pad_all(idle_group, 0, 0);
    lv_obj_clear_flag(idle_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    const int character_y = L.small_icons ? -43 : -63;
    idle_claude_img = splash_mini_create(idle_group, "idle blink", L.idle_px);
    if (idle_claude_img) lv_obj_align(idle_claude_img, LV_ALIGN_CENTER, -L.scr_w / 4, character_y);
    idle_codex_img = lv_image_create(idle_group);
    lv_image_set_src(idle_codex_img, &chatgpt_logo_dsc);
    lv_image_set_scale(idle_codex_img, 384);
    lv_obj_align(idle_codex_img, LV_ALIGN_CENTER, L.scr_w / 4, character_y);
    idle_ball = lv_obj_create(idle_group);
    lv_obj_remove_style_all(idle_ball);
    const int diameter = L.small_icons ? 10 : 18;
    lv_obj_set_size(idle_ball, diameter, diameter);
    lv_obj_set_style_radius(idle_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(idle_ball, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(idle_ball, LV_OPA_COVER, 0);
    lv_obj_add_flag(idle_ball, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(idle_ball, 16);
    lv_obj_add_event_cb(idle_ball, [](lv_event_t*) { ball_tapped = true; }, LV_EVENT_PRESSED, nullptr);
    if (L.small_icons) {
        init_icon_dsc_rgb565a8(&waiting_message_dsc, waiting_small_width, waiting_small_height, waiting_small);
    } else {
        init_icon_dsc_rgb565a8(&waiting_message_dsc, waiting_large_width, waiting_large_height, waiting_large);
    }
    wait_message = lv_image_create(idle_group);
    lv_image_set_src(wait_message, &waiting_message_dsc);
    lv_obj_align(wait_message, LV_ALIGN_BOTTOM_MID, 0, L.small_icons ? -20 : -42);
    game_label = lv_label_create(idle_group);
    lv_obj_set_style_text_font(game_label, L.title_font, 0);
    lv_obj_set_style_text_color(game_label, COL_TEXT, 0);
    lv_obj_align(game_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_hidden(game_label, true);
    game_instruction = lv_label_create(idle_group);
    lv_label_set_text_static(game_instruction, "Тапни, когда мяч справа");
    lv_obj_set_style_text_font(game_instruction, &notes_font, 0);
    lv_obj_set_style_text_color(game_instruction, COL_TEXT, 0);
    lv_obj_align(game_instruction, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_set_hidden(game_instruction, true);
    game_target = lv_obj_create(idle_group);
    lv_obj_remove_style_all(game_target);
    lv_obj_set_size(game_target, L.small_icons ? 26 : 48, L.small_icons ? 26 : 48);
    lv_obj_set_style_radius(game_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(game_target, 2, 0);
    lv_obj_set_style_border_color(game_target, COL_DIM, 0);
    lv_obj_clear_flag(game_target, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(game_target, LV_ALIGN_CENTER, L.scr_w / 4, character_y);
    lv_obj_set_hidden(game_target, true);

    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);  // update_view_state decides
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, L.title_font, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    // The nudge balances the corner logo on the left; smaller on small
    // screens where the logo is 40px and the battery icon sits closer.
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, L.title_nudge, L.title_y);

    // Usage panels (shown when connected) live in a transparent full-size group
    // so they can be toggled against the pairing hint as one unit.
    usage_group = lv_obj_create(usage_container);
    lv_obj_set_size(usage_group, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_group, 0, 0);
    lv_obj_set_style_bg_opa(usage_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_group, 0, 0);
    lv_obj_set_style_pad_all(usage_group, 0, 0);
    lv_obj_clear_flag(usage_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_EVENT_BUBBLE);

    panel_session = make_usage_panel(usage_group, L.content_y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);
    lbl_session_reset_clock = make_reset_clock(panel_session);
    lbl_limit_timer = lv_label_create(panel_session);
    lv_obj_set_style_text_font(lbl_limit_timer, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_limit_timer, COL_TEXT, 0);
    lv_obj_set_pos(lbl_limit_timer, 0, L.usage_bar_y);
    lv_obj_add_flag(lbl_limit_timer, LV_OBJ_FLAG_HIDDEN);
    if (!L.small_icons) {
        bounce_canvas = lv_canvas_create(panel_session);
        lv_canvas_set_buffer(bounce_canvas, bounce_pixels, 112, 48, LV_COLOR_FORMAT_RGB565);
        lv_obj_align(bounce_canvas, LV_ALIGN_TOP_RIGHT, 0, L.usage_reset_y - 48 - 4);
        lv_obj_add_flag(bounce_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    // Enterprise-only overlays inside panel_session — hidden until enterprise data arrives
    lbl_session_pct_sym = lv_label_create(panel_session);
    lv_label_set_text(lbl_session_pct_sym, "%");
    lv_obj_set_style_text_font(lbl_session_pct_sym, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_session_pct_sym, COL_TEXT, 0);
    lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_desc = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_desc, "of your monthly budget");
    lv_obj_set_style_text_font(lbl_spending_desc, L.reset_font, 0);
    lv_obj_set_style_text_color(lbl_spending_desc, COL_DIM, 0);
    lv_obj_set_pos(lbl_spending_desc, 0, L.usage_reset_y);
    lv_obj_add_flag(lbl_spending_desc, LV_OBJ_FLAG_HIDDEN);

    lbl_spending_status = lv_label_create(panel_session);
    lv_label_set_text(lbl_spending_status, "");
    lv_obj_set_style_text_font(lbl_spending_status, L.pace_font, 0);
    lv_obj_set_pos(lbl_spending_status, 0, L.usage_reset_y + 20);
    lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);

    panel_weekly = make_usage_panel(usage_group,
                     L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);
    lbl_weekly_reset_clock = make_reset_clock(panel_weekly);
    // Recolor enabled so enterprise period box can color pace and reset separately
    lv_label_set_recolor(lbl_weekly_reset, true);

    build_pair_group(usage_container);
    build_idle_group(usage_container);

    // Status line — always visible on the usage view. Driven by ui_tick_anim().
    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, L.anim_font, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, L.anim_y);
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (L.small_icons) init_icon_dsc_rgb565a8(&logo_dsc, LOGO_SMALL_WIDTH, LOGO_SMALL_HEIGHT, logo_small_data);
    else               init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    if (L.small_icons) init_icon_dsc_rgb565a8(&chatgpt_logo_dsc, 40, 40, chatgpt_logo_40);
    else               init_icon_dsc_rgb565a8(&chatgpt_logo_dsc, 80, 80, chatgpt_logo_80);
    init_battery_icons();

    init_usage_screen(scr);
    build_notes_page(scr);
    splash_init(scr);
    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, L.margin, L.logo_y);

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - L.batt_w - L.margin, L.batt_y);
    // Boards without battery telemetry never show the indicator (per the HAL
    // contract; previously every board drew the empty-battery glyph).
    if (!board_caps().has_battery) {
        lv_obj_del(battery_img);
        battery_img = nullptr;
    } else {
        lv_obj_add_flag(battery_img, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(battery_img, 7);
        battery_percent_label = lv_label_create(battery_img);
        lv_obj_set_style_text_font(battery_percent_label,
                                  L.small_icons ? &font_styrene_12 : &font_styrene_14, 0);
        lv_obj_set_style_text_color(battery_percent_label, COL_TEXT, 0);
        lv_obj_set_style_bg_color(battery_percent_label, COL_BG, 0);
        lv_obj_set_style_bg_opa(battery_percent_label, LV_OPA_COVER, 0);
        lv_label_set_text_static(battery_percent_label, "--%");
        lv_obj_align(battery_percent_label, LV_ALIGN_CENTER, -1, 0);
        lv_obj_set_hidden(battery_percent_label, true);
        lv_obj_add_event_cb(battery_img, [](lv_event_t*) {
            battery_tapped_ms = lv_tick_get();
            lv_obj_set_hidden(battery_percent_label, false);
        }, LV_EVENT_CLICKED, nullptr);
    }

    // Last, so the charge overlay covers everything else when it plays.
    charge_anim_init(scr);
}

static bool session_exhausted(const UsageData& data) {
    return !data.enterprise && (data.session_pct >= 100.0f || strcmp(data.status, "rejected") == 0);
}

static void tick_limit_timer(uint32_t now) {
    const UsageData& data = demo_active ? demo_usage : usage_by_screen[current_screen];
    int seconds = data.session_reset_secs;
    if (seconds < 0 && data.session_reset_mins >= 0) seconds = data.session_reset_mins * 60;
    if (seconds < 0) {
        lv_label_set_text(lbl_limit_timer, "Reset time pending");
        return;
    }
    const int elapsed = (now - (demo_active ? demo_started_ms : last_data_ms[current_screen])) / 1000;
    seconds = seconds > elapsed ? seconds - elapsed : 0;
    lv_label_set_text_fmt(lbl_limit_timer, "%02d:%02d:%02d",
                          seconds / 3600, seconds / 60 % 60, seconds % 60);
}

static void bounce_rect(int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; ++row) {
        for (int col = x; col < x + w; ++col) bounce_pixels[row][col] = color;
    }
}

static void tick_bounce(uint32_t now) {
    if (!bounce_canvas) return;
    const unsigned phase = (now / 50) % 36;
    const uint16_t orange = lv_color_to_u16(COL_ACCENT);
    const uint16_t pale = lv_color_to_u16(COL_TEXT);
    const uint16_t dark = lv_color_to_u16(COL_PANEL);
    bounce_rect(0, 0, 112, 48, dark);
    bounce_rect(105, 3, 3, 40, lv_color_to_u16(COL_DIM));
    bounce_rect(10, 19, 30, 17, orange);
    bounce_rect(13, 14, 24, 22, orange);
    bounce_rect(6, 23, 8, 4, orange);
    bounce_rect(39, phase < 6 || phase > 24 ? 25 : 19, 9, 4, orange);
    for (int x = 12; x <= 33; x += 7) bounce_rect(x, 35, 4, 6, orange);
    bounce_rect(20, 20, 3, 4, 0);
    bounce_rect(31, 20, 3, 4, 0);
    const int flight = phase < 6 || phase > 29 ? 0 : phase <= 17 ? phase - 6 : 29 - phase;
    const int ball_x = 45 + 54 * flight / 11;
    const int ball_y = 24 - 10 * flight / 11;
    bounce_rect(ball_x + 1, ball_y, 4, 6, pale);
    bounce_rect(ball_x, ball_y + 1, 6, 4, pale);
    if (phase == 17 || phase == 18) {
        bounce_rect(98, 9, 3, 2, orange);
        bounce_rect(98, 22, 3, 2, orange);
    }
    lv_obj_invalidate(bounce_canvas);
}

static void render_usage(const UsageData* data) {
    int s_pct = (int)(data->session_pct + 0.5f);
    lv_label_set_text(lbl_session_reset_clock, data->enterprise ? "" : data->session_reset_clock);
    lv_label_set_text(lbl_weekly_reset_clock, data->enterprise ? "" : data->weekly_reset_clock);

    if (data->enterprise) {
        // Spending box: big number-only label + small "%" symbol + desc + pace
        lv_obj_set_style_text_font(lbl_session_pct, L.ent_pct_font, 0);
        lv_label_set_text(lbl_session_label, "Spending");
        lv_obj_add_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status,   LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_text_font(lbl_session_pct, L.pct_font, 0);
        lv_label_set_text(lbl_session_label, current_screen == SCREEN_CODEX ? "5-hour" : "Current");
        lv_obj_clear_flag(lbl_session_reset, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_session_pct_sym, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_desc,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_spending_status, LV_OBJ_FLAG_HIDDEN);
        if (panel_weekly) lv_obj_clear_flag(panel_weekly, LV_OBJ_FLAG_HIDDEN);
    }

    char buf[48];

    // Pace vars used in both enterprise blocks below
    const char* pace_text = "Under pace";
    lv_color_t  pace_color = COL_GREEN;
    const char* pace_hex   = "788c5d";   // matches THEME_GREEN
    if (data->session_pct > (float)data->time_pct + 15.0f) {
        pace_text = "Over pace";  pace_color = COL_RED;   pace_hex = "c0392b";
    } else if (data->session_pct > (float)data->time_pct - 15.0f) {
        pace_text = "On pace";    pace_color = COL_AMBER; pace_hex = "d97757";
    }

    if (data->enterprise) {
        lv_label_set_text_fmt(lbl_session_pct, "%d", s_pct);
        lv_obj_align_to(lbl_session_pct_sym, lbl_session_pct,
                        LV_ALIGN_OUT_RIGHT_TOP, 4, 12);
    } else {
        lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
        format_reset_time(data->session_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_session_reset, buf);
    }

    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);
    const bool exhausted = session_exhausted(*data);
    lv_obj_set_hidden(bar_session, exhausted);
    lv_obj_set_hidden(lbl_limit_timer, !exhausted);
    if (bounce_canvas) lv_obj_set_hidden(bounce_canvas, !exhausted);
    if (exhausted) {
        lv_label_set_text_static(lbl_session_reset, "Resets in");
        lv_obj_move_foreground(lbl_limit_timer);
        tick_limit_timer(lv_tick_get());
    }

    if (data->enterprise) {
        // Period box: time % + dynamic pace color + "Resets <date>" label
        lv_label_set_text(lbl_weekly_label, "Period");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", data->time_pct);
        lv_bar_set_value(bar_weekly, data->time_pct, LV_ANIM_ON);
        lv_color_t bar_pace = (data->session_pct <= (float)data->time_pct) ? COL_GREEN :
                              (data->session_pct <= (float)data->time_pct + 15.0f) ? COL_AMBER :
                              COL_RED;
        lv_obj_set_style_bg_color(bar_weekly, bar_pace, LV_PART_INDICATOR);
        snprintf(buf, sizeof(buf), "#%s %s# - #faf9f5 Resets %s#",
                 pace_hex, pace_text, data->reset_date);
        lv_label_set_text(lbl_weekly_reset, buf);
    } else {
        int w_pct = (int)(data->weekly_pct + 0.5f);
        lv_label_set_text(lbl_weekly_label, "Weekly");
        lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
        lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
        lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);
        format_reset_time(data->weekly_reset_mins, buf, sizeof(buf));
        lv_label_set_text(lbl_weekly_reset, buf);
    }
}

void ui_update(const UsageData* data, screen_t screen) {
    if (!data->valid) return;
    usage_by_screen[screen] = *data;
    last_data_ms[screen] = lv_tick_get();
    if (screen == SCREEN_USAGE) {
        if (clock_base_epoch && !data->clock_epoch) {
            clock_last_min = -1;
            if (!demo_active && current_screen == SCREEN_USAGE) lv_label_set_text(lbl_title, "Claude");
        }
        clock_base_epoch = data->clock_epoch;
        clock_base_ms = last_data_ms[screen];
        clock_fmt = data->clock_fmt;
    }
    if (!demo_active && current_screen == screen) render_usage(data);
}

// Pick the usage-view sub-screen: pairing hint (BLE down), the idle "Zzz" screen
// (connected but data has gone stale), or the live usage panels. Only re-lays-out
// on an actual change. The animated status line stays visible everywhere — it
// reads "Listening…" on the idle screen, keeping it alive rather than frozen.
static void update_view_state(void) {
    if (ball_game) return;
    if (current_screen == SCREEN_NOTES) return;
    if (!usage_group || !pair_group || !idle_group) return;
    int v;
    if (demo_active) {
        v = 2;
    } else if (usage_by_screen[current_screen].valid) {
        v = s_ble_connected && lv_tick_get() - last_data_ms[current_screen] < DATA_FRESH_MS ? 2 : 3;
    } else if (!s_ble_connected) {
        v = 0;  // pairing hint
#ifdef BOARD_AMOLED_216_C6
        if (wifi_sync_configured()) v = 1;
#endif
    } else {
        v = 1;  // shared waiting view
    }
    lv_obj_set_hidden(lbl_title, v == 1);
    if (logo_img && !showing_splash) lv_obj_set_hidden(logo_img, v == 1);
    if (v == view_state) return;
    view_state = v;
    lv_obj_set_hidden(lbl_anim, v == 1);
    lv_obj_set_style_opa(usage_group, v == 3 ? LV_OPA_60 : LV_OPA_COVER, 0);
    lv_obj_add_flag(pair_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(idle_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(usage_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(v == 0 ? pair_group : v == 1 ? idle_group : usage_group,
                      LV_OBJ_FLAG_HIDDEN);
}

static unsigned thinking_phase = 0;
static uint32_t thinking_last_ms = 0;
static const char* const thinking_frames[] = {"Thinking...", "Thinking..", "Thinking."};

static void tick_waiting_animation(uint32_t now) {
    splash_mini_tick();
    struct Throw {
        int flight_ms;
        int hold_ms;
        int height;
    };
    static constexpr Throw throws[] = {
        {760, 250, 68}, {1120, 520, 105}, {520, 170, 30},
        {850, 330, 54}, {660, 650, 45}, {1040, 220, 88}, {590, 390, 38},
    };
    static uint32_t last_frame = 0;
    static uint32_t started = 0;
    static unsigned index = 0;
    static int direction = 1;
    if (now - last_frame < 33) return;
    last_frame = now;
    if (!started) started = now;
    int elapsed = static_cast<int>(now - started);
    const bool miss = index == 4 && direction > 0;
    if (elapsed >= throws[index].flight_ms + (miss ? 1200 : throws[index].hold_ms)) {
        started = now;
        elapsed = 0;
        index = (index + 1) % (sizeof(throws) / sizeof(throws[0]));
        direction = -direction;
    }
    const Throw& move = throws[index];
    if (ball_tapped && elapsed < move.flight_ms) {
        started -= 110;
        const int accelerated = elapsed + 110;
        elapsed = accelerated < move.flight_ms ? accelerated : move.flight_ms;
    }
    ball_tapped = false;
    const bool caught = elapsed >= move.flight_ms;
    const int phase = caught ? 100 : elapsed * 100 / move.flight_ms;
    const int catch_ms = caught ? elapsed - move.flight_ms : 0;
    const int recoil = caught && catch_ms < 140
        ? (70 - abs(catch_ms - 70)) / 7 : 0;
    const int half_gap = L.scr_w / 7;
    const int character_y = L.small_icons ? -43 : -63;
    const int height = L.small_icons ? move.height / 2 : move.height;
    const int arc = (4 * height * phase * (100 - phase)) / 10000;
    int x = direction * (2 * half_gap * phase / 100 - half_gap);
    const int diameter = L.small_icons ? 10 : 18;
    const int squash = recoil / 3;
    lv_obj_set_size(idle_ball, diameter + squash, diameter - squash);
    int runaway_drop = 0;
    if (index == 4 && direction > 0 && caught) {
        const int roll = catch_ms <= 600 ? catch_ms : 1200 - catch_ms;
        x += (L.scr_w / 2 - half_gap + 12) * roll / 600;
        runaway_drop = roll * (L.small_icons ? 12 : 24) / 600;
    }
    lv_obj_align(idle_ball, LV_ALIGN_CENTER, x + direction * recoil / 2,
                 character_y - arc + recoil / 2 + runaway_drop);
    if (idle_claude_img) {
        lv_obj_align(idle_claude_img, LV_ALIGN_CENTER, -L.scr_w / 4 - (direction < 0 ? recoil / 2 : 0),
                     character_y + (direction < 0 ? recoil : 0));
    }
    lv_obj_align(idle_codex_img, LV_ALIGN_CENTER, L.scr_w / 4 + (direction > 0 ? recoil / 2 : 0),
                 character_y + (direction > 0 ? recoil : 0));
    lv_image_set_rotation(idle_codex_img, direction > 0 ? recoil * 9 : 0);
}

static void tick_ball_game(uint32_t now) {
    splash_mini_tick();
    if (now - game_last_frame < 20) return;
    game_last_frame = now;

    struct Serve { int windup; int duration; int height; };
    static constexpr Serve serves[] = {
        {570, 1040, 56}, {360, 690, 22}, {760, 1340, 96},
        {450, 860, 48}, {880, 760, 64}, {310, 1180, 75},
    };
    const Serve& serve = serves[game_serve];
    const int speedup = game_streak < 8 ? game_streak * 22 : 176;
    const int duration = serve.duration - speedup;
    int elapsed = now - game_started_ms;
    const int range = L.scr_w / 4;
    const int center = L.small_icons ? -43 : -63;
    const int height = L.small_icons ? serve.height / 2 : serve.height;
    const int diameter = L.small_icons ? 10 : 18;
    int x = -range;
    int y = center;
    int squash = 0;
    int clawd_bob = 0;
    bool hit_window = false;
    lv_color_t ball_color = COL_ACCENT;
    const char* instruction = "Clawd готовится";

    const GamePhase frame_phase = game_phase;
    switch (frame_phase) {
    case GamePhase::Ready:
        clawd_bob = elapsed * 12 / serve.windup;
        y += clawd_bob;
        if (elapsed >= serve.windup) {
            game_started_ms = now;
            game_phase = GamePhase::Serve;
        }
        break;
    case GamePhase::Serve: {
        const int progress = elapsed < duration ? elapsed * 100 / duration : 100;
        x = -range + 2 * range * progress / 100;
        y -= 4 * height * progress * (100 - progress) / 10000;
        hit_window = elapsed >= duration - 110 && elapsed <= duration + 240;
        instruction = hit_window ? "Бей!" : "Жди мяч";
        if (ball_tapped && hit_window) {
            game_contact_x = x;
            game_contact_y = y;
            game_started_ms = now;
            game_phase = GamePhase::Return;
            ++game_hits;
            ++game_streak;
            lv_label_set_text_fmt(game_label, "%u  ·  x%u", game_hits, game_streak);
        } else if (elapsed > duration + 240) {
            game_started_ms = now;
            game_phase = GamePhase::Miss;
            game_streak = 0;
            lv_label_set_text_fmt(game_label, "%u", game_hits);
        }
        break;
    }
    case GamePhase::Return: {
        const int progress = elapsed < 620 ? elapsed * 100 / 620 : 100;
        x = game_contact_x - (game_contact_x + range) * progress / 100;
        y = game_contact_y + (center - game_contact_y) * progress / 100
            - 3 * height * progress * (100 - progress) / 10000;
        squash = elapsed < 100 ? 4 - elapsed / 25 : 0;
        clawd_bob = progress >= 90 ? (progress - 90) / 2 : 0;
        ball_color = lv_color_hex(0x58D97C);
        instruction = game_streak >= 3 ? "Отличная серия!" : "Отбито!";
        break;
    }
    case GamePhase::Miss:
        x = range + elapsed * range / 650;
        y += elapsed * elapsed / 7500;
        instruction = "Мимо!";
        break;
    }
    if ((frame_phase == GamePhase::Return && elapsed >= 770) ||
        (frame_phase == GamePhase::Miss && elapsed >= 700)) {
        game_phase = GamePhase::Ready;
        game_started_ms = now;
        game_serve = (game_serve + 1 + now % 5) % (sizeof(serves) / sizeof(serves[0]));
    }
    ball_tapped = false;
    lv_obj_set_size(idle_ball, diameter + squash, diameter - squash);
    lv_obj_align(idle_ball, LV_ALIGN_CENTER, x, y);
    lv_obj_set_style_bg_color(idle_ball, ball_color, 0);
    lv_obj_set_style_border_color(game_target, hit_window ? lv_color_hex(0x58D97C) : COL_DIM, 0);
    if (idle_claude_img) lv_obj_align(idle_claude_img, LV_ALIGN_CENTER, -range, center + clawd_bob);
    if (strcmp(lv_label_get_text(game_instruction), instruction)) {
        lv_label_set_text_static(game_instruction, instruction);
    }
}

void ui_tick_anim(void) {
    if (battery_percent_label && !lv_obj_has_flag(battery_percent_label, LV_OBJ_FLAG_HIDDEN) &&
        lv_tick_get() - battery_tapped_ms >= 5000) {
        lv_obj_set_hidden(battery_percent_label, true);
    }
    if (showing_splash) return;
    if (idle_is_asleep()) {
        page_started_ms = lv_tick_get();
        return;
    }
    if (ball_game) {
        tick_ball_game(lv_tick_get());
        return;
    }
    if (!demo_active && !page_pinned && lv_tick_get() - page_started_ms >= page_delay_ms) {
        auto next = static_cast<screen_t>((current_screen + 1) % SCREEN_COUNT);
        ui_show_screen(next);
    }
    if (current_screen == SCREEN_NOTES) return;
    update_view_state();
    uint32_t now = lv_tick_get();
    if (view_state == 1) {
        tick_waiting_animation(now);
        return;
    }
    const UsageData& usage = demo_active ? demo_usage : usage_by_screen[current_screen];
    if (view_state == 3) {
        const uint32_t minutes = (now - last_data_ms[current_screen]) / 60000;
        if (minutes == 0) lv_label_set_text_static(lbl_anim, "Last update <1m ago");
        else {
            char age[32];
            snprintf(age, sizeof(age), "Updated %lum ago", static_cast<unsigned long>(minutes));
            if (strcmp(lv_label_get_text(lbl_anim), age)) lv_label_set_text(lbl_anim, age);
        }
        return;
    }
    if (view_state == 2 && session_exhausted(usage)) {
        static uint32_t last_timer_tick = 0;
        if (now - last_timer_tick >= 1000) {
            last_timer_tick = now;
            tick_limit_timer(now);
        }
        static uint32_t last_bounce_tick = 0;
        if (now - last_bounce_tick >= 50) {
            last_bounce_tick = now;
            tick_bounce(now);
        }
    }

    if (current_screen == SCREEN_CODEX && !demo_active) {
        if (now - thinking_last_ms >= 500) {
            thinking_last_ms = now;
            thinking_phase = (thinking_phase + 1) % 3;
            lv_label_set_text_static(lbl_anim, thinking_frames[thinking_phase]);
        }
        return;
    }

    // Title clock: once the daemon has sent wall-clock time, replace "Usage" with
    // the live time, advanced locally so it ticks every minute between payloads.
    if (current_screen == SCREEN_USAGE && (demo_active || clock_base_epoch > 0)) {
        time_t cur = demo_active ? demo_start_minutes * 60 + (now - demo_started_ms) / 1000
                                 : clock_base_epoch + (now - clock_base_ms) / 1000;
        struct tm tmv;
        gmtime_r(&cur, &tmv);   // epoch is already local wall-clock → gmtime keeps it as-is
        if (tmv.tm_min != clock_last_min) {   // only rewrite the title when the minute changes
            clock_last_min = tmv.tm_min;
            char tbuf[12];
            if (!demo_active && clock_fmt == 12) {
                int h12 = tmv.tm_hour % 12;
                if (h12 == 0) h12 = 12;
                snprintf(tbuf, sizeof(tbuf), "%d:%02d %s", h12, tmv.tm_min,
                         tmv.tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
            }
            lv_label_set_text(lbl_title, tbuf);
        }
    }

    if (demo_active) return;

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms < spinner_ms[anim_spinner_idx]) return;
    anim_last_ms = now;
    anim_phase = (anim_phase + 1) % SPINNER_PHASES;
    anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                    : (SPINNER_PHASES - anim_phase);

    // Status text by priority. Whimsical messages only when connected & settled.
    const char* text;
    if (!s_ble_connected) {
        text = "Waiting";              // advertising / waiting for a host connection
    } else if (view_state == 1) {      // idle — alternate so it reads as alive AND data-less
        text = (anim_msg_idx & 1) ? "No data" : "Listening";
    } else if (now - connected_at_ms < 5000) {
        text = "Connected";
    } else {
        text = anim_messages[anim_msg_idx];
    }

    // All states share the whimsical style: "<glyph> <Title-case word>…"
    static char buf[80];
    snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
             spinner_frames[anim_spinner_idx], text);
    lv_label_set_text(lbl_anim, buf);
}

static void show_screen(screen_t screen) {
    if (ball_game) {
        ball_game = false;
        lv_obj_set_hidden(idle_codex_img, false);
        lv_obj_set_hidden(wait_message, false);
        lv_obj_set_hidden(game_label, true);
        lv_obj_set_hidden(game_instruction, true);
        lv_obj_set_hidden(game_target, true);
        lv_obj_set_style_bg_color(idle_ball, COL_ACCENT, 0);
        view_state = -1;
    }
    page_started_ms = lv_tick_get();
    page_delay_ms = page_interval_ms;
    page_pinned = false;
    const screen_t previous_screen = current_screen;
    current_screen = screen;
    showing_splash = false;
    splash_hide();
    if (battery_img) lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    const bool show_notes = screen == SCREEN_NOTES;
    lv_obj_set_hidden(notes_page, !show_notes);
    lv_obj_set_hidden(usage_container, show_notes);
    lv_obj_set_hidden(logo_img, show_notes);
    if (show_notes) return;
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(logo_img, screen == SCREEN_CODEX ? &chatgpt_logo_dsc : &logo_dsc);
    lv_label_set_text(lbl_title, screen == SCREEN_CODEX ? "Codex" : "Claude");
    lv_obj_set_style_text_color(lbl_anim, screen == SCREEN_CODEX ? lv_color_white() : COL_ACCENT, 0);
    if (screen == SCREEN_CODEX) {
        thinking_phase = 0;
        thinking_last_ms = lv_tick_get();
        lv_label_set_text_static(lbl_anim, thinking_frames[0]);
    } else if (previous_screen == SCREEN_CODEX) {
        lv_label_set_text(lbl_anim, "Listening");
    }
    clock_last_min = -1;
    if (demo_active) {
        render_usage(&demo_usage);
        lv_label_set_text_fmt(lbl_anim, "Demo %u/4", demo_index + 1);
    } else if (usage_by_screen[screen].valid) {
        render_usage(&usage_by_screen[screen]);
    }
    update_view_state();
}

void ui_show_screen(screen_t screen) {
    demo_active = false;
    show_screen(screen);
}

static void show_demo_scenario(screen_t screen) {
    struct Scenario {
        int session_used;
        int weekly_used;
        int session_minutes;
        int weekly_minutes;
    };
    static constexpr Scenario scenarios[] = {
        {90, 90, 30, 17 * 60},
        {50, 50, 150, 3 * 1440 + 12 * 60},
        {10, 10, 270, 6 * 1440 + 7 * 60},
        {100, 75, 25, 1440 + 18 * 60},
    };
    const auto& scenario = scenarios[demo_index];
    demo_usage = {};
    demo_usage.valid = true;
    demo_usage.ok = true;
    demo_usage.session_pct = scenario.session_used;
    demo_usage.weekly_pct = scenario.weekly_used;
    demo_usage.session_reset_mins = scenario.session_minutes;
    demo_usage.weekly_reset_mins = scenario.weekly_minutes;
    demo_usage.session_reset_secs = scenario.session_minutes * 60;
    demo_usage.weekly_reset_secs = scenario.weekly_minutes * 60;
    strlcpy(demo_usage.status, scenario.session_used == 100 ? "rejected" : "allowed",
            sizeof(demo_usage.status));
    const auto format_clock = [](char (&label)[6], int remaining) {
        const unsigned minutes = (demo_start_minutes + remaining) % 1440;
        snprintf(label, sizeof(label), "%02u:%02u", minutes / 60, minutes % 60);
    };
    format_clock(demo_usage.session_reset_clock, scenario.session_minutes);
    format_clock(demo_usage.weekly_reset_clock, scenario.weekly_minutes);
    demo_started_ms = lv_tick_get();
    show_screen(screen);
}

void ui_toggle_demo() {
    if (demo_active) {
        ui_show_screen(demo_return_screen);
        ui_set_pinned(demo_return_pin);
        return;
    }
    demo_return_screen = current_screen;
    demo_return_pin = page_pinned;
    demo_active = true;
    demo_index = 0;
    show_demo_scenario(current_screen == SCREEN_CODEX ? SCREEN_CODEX : SCREEN_USAGE);
}

void ui_demo_next_scenario() {
    demo_index = (demo_index + 1) % 4;
    show_demo_scenario(current_screen);
}

void ui_demo_switch_provider() {
    show_screen(current_screen == SCREEN_CODEX ? SCREEN_USAGE : SCREEN_CODEX);
}

void ui_start_ball_game(void) {
    ui_show_screen(SCREEN_USAGE);
    ball_game = true;
    game_hits = 0;
    game_streak = 0;
    game_serve = 0;
    game_last_frame = 0;
    game_phase = GamePhase::Ready;
    ball_tapped = false;
    game_started_ms = lv_tick_get();
    lv_obj_set_hidden(pair_group, true);
    lv_obj_set_hidden(usage_group, true);
    lv_obj_set_hidden(idle_group, false);
    lv_obj_set_hidden(logo_img, true);
    lv_obj_set_hidden(lbl_title, true);
    lv_obj_set_hidden(lbl_anim, true);
    lv_obj_set_hidden(idle_codex_img, true);
    lv_obj_set_hidden(wait_message, true);
    lv_obj_set_hidden(game_label, false);
    lv_obj_set_hidden(game_instruction, false);
    lv_obj_set_hidden(game_target, false);
    lv_label_set_text_static(game_label, "0");
}

void ui_toggle_splash(void) {
    if (demo_active) ui_show_screen(current_screen);
    if (ball_game) {
        ui_show_screen(current_screen);
        return;
    }
    if (showing_splash) {
        ui_show_screen(current_screen);
        Serial.println("BOOT: limits");
        return;
    }
    showing_splash = true;
    lv_obj_set_hidden(notes_page, true);
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    if (battery_img) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    splash_show();
    Serial.println("BOOT: splash");
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    (void)name; (void)mac;
    bool was_connected = s_ble_connected;
    s_ble_connected = (state == BLE_STATE_CONNECTED);

    if (s_ble_connected && !was_connected) connected_at_ms = lv_tick_get();
    // pair / idle / usage — picked from connection + data freshness.
    update_view_state();
}

void ui_update_battery(int percent, bool charging) {
    if (!battery_img) return;
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    if (percent < 0) lv_label_set_text_static(battery_percent_label, "--%");
    else lv_label_set_text_fmt(battery_percent_label, "%d%%", percent);
}
