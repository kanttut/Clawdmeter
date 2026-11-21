#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_USAGE,
    SCREEN_CODEX,
    SCREEN_NOTES,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data, screen_t screen = SCREEN_USAGE);
void ui_tick_anim(void);
void ui_note_touch(bool pressed);
void ui_show_screen(screen_t screen);
bool ui_is_demo(void);
void ui_toggle_demo(void);
void ui_demo_next_scenario(void);
void ui_demo_switch_provider(void);
void ui_toggle_splash(void);
void ui_start_ball_game(void);
void ui_set_pinned(bool pinned);
void ui_set_page_interval(unsigned seconds);
bool ui_is_pinned(void);
unsigned ui_get_page_interval(void);
const char* ui_get_mode(void);
int ui_get_usage_age(screen_t screen);
uint32_t ui_get_note_checks(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);
void ui_update_note(const char* text, uint32_t done = 0);
