#include "sleep_guard.h"

void sleep_guard_enter(SleepGuard* g) {
    if (!g || g->inited) return;

    memset(g, 0, sizeof(*g));

    bool before = false;
    g->rc_get_before = appletIsAutoSleepDisabled(&before);
    if (R_SUCCEEDED(g->rc_get_before)) {
        g->have_original = true;
        g->was_disabled = before;
    } else {
        g->have_original = false;
        g->was_disabled = false;
    }

    g->rc_set_disable = appletSetAutoSleepDisabled(true);

    bool after = false;
    g->rc_get_after = appletIsAutoSleepDisabled(&after);
    g->is_disabled = R_SUCCEEDED(g->rc_get_after) ? after : false;

    g->inited = true;
}

void sleep_guard_leave(SleepGuard* g) {
    if (!g || !g->inited) return;

    /* Best-effort restore. If we could not read the original state, default to enabling sleep. */
    if (g->have_original) g->rc_restore = appletSetAutoSleepDisabled(g->was_disabled);
    else g->rc_restore = appletSetAutoSleepDisabled(false);
}
