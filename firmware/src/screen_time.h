#pragma once
#include <stdint.h>

void screen_time_init();
void screen_time_tick(bool lit);
void screen_time_flush();
void screen_time_set_clock(uint32_t local_epoch);
struct ScreenTimeTotals {
    uint32_t today;
    uint32_t week;
    uint32_t total;
};
ScreenTimeTotals screen_time_totals();
void screen_time_update_power(bool connected);
int32_t screen_time_since_charge();
