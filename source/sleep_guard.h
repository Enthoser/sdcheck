#pragma once
#include "app.h"

typedef struct {
    bool inited;
    bool have_original;
    bool was_disabled;
    bool is_disabled;

    Result rc_get_before;
    Result rc_set_disable;
    Result rc_get_after;
    Result rc_restore;
} SleepGuard;

void sleep_guard_enter(SleepGuard* g);
void sleep_guard_leave(SleepGuard* g);
