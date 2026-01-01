#include "app.h"
#include "util.h"
#include "log.h"
#include "config.h"
#include "sleep_guard.h"
#include "scan_engine.h"

/* --------------------------------------------------------------------------
   Sleep guard
----------------------------------------------------------------------------*/
static SleepGuard g_sleep;

/* --------------------------------------------------------------------------
   Helpers
----------------------------------------------------------------------------*/
static inline uint64_t poll_down(PadState* pad) {
    padUpdate(pad);
    return padGetButtonsDown(pad);
}

static inline bool is_cancel_mask(uint64_t held) {
    return (held & (HidNpadButton_Plus | HidNpadButton_Minus | HidNpadButton_B)) != 0;
}

static void ui_wait_release(PadState* pad, uint64_t mask, uint64_t timeout_ms) {
    if (!pad) return;
    uint64_t start = now_ms();
    while (appletMainLoop()) {
        padUpdate(pad);
        if ((padGetButtons(pad) & mask) == 0) break;
        if (timeout_ms && (now_ms() - start) > timeout_ms) break;
        svcSleepThread(10 * 1000 * 1000);
    }
}

static inline void ui_goto(int row, int col) {
    printf("\x1b[%d;%dH", row + g_ui.top_margin, col);
}

static inline void ui_clear_screen(void) { printf("\x1b[2J"); }
static inline void ui_hide_cursor(void) { printf("\x1b[?25l"); }
static inline void ui_show_cursor(void) { printf("\x1b[?25h"); }

static void ui_draw_box(int x, int y, int w, int h, const char* title, const char* title_color) {
    if (w < 4 || h < 3) return;

    ui_goto(y, x);
    printf("%s+", C_GRAY);
    for (int i = 0; i < w - 2; i++) putchar('-');
    printf("+%s", C_RESET);

    for (int r = 1; r < h - 1; r++) {
        ui_goto(y + r, x);
        printf("%s|%s", C_GRAY, C_RESET);
        for (int i = 0; i < w - 2; i++) putchar(' ');
        printf("%s|%s", C_GRAY, C_RESET);
    }

    ui_goto(y + h - 1, x);
    printf("%s+", C_GRAY);
    for (int i = 0; i < w - 2; i++) putchar('-');
    printf("+%s", C_RESET);

    if (title && title[0]) {
        char tbuf[64];
        snprintf(tbuf, sizeof(tbuf), " %s ", title);
        int maxw = w - 4;
        if ((int)strlen(tbuf) > maxw) tbuf[maxw] = 0;
        ui_goto(y, x + 2);
        printf("%s%s%s%s", title_color ? title_color : "", C_BOLD, tbuf, C_RESET);
    }
}

static void ui_print_fit(int row, int col, int w, const char* color, const char* fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    for (size_t i = 0; tmp[i]; i++) {
        if (tmp[i] == '\n' || tmp[i] == '\r') { tmp[i] = 0; break; }
    }

    ui_goto(row, col);
    if (color) printf("%s", color);
    printf("%-*.*s", w, w, tmp);
    printf("%s", C_RESET);
}

static void ui_draw_header(const char* screen_title, const char* hint_lines) {
    ui_hide_cursor();
    ui_clear_screen();

    char title[96];
    snprintf(title, sizeof(title), "SD Check - %s", screen_title ? screen_title : " ");
    ui_draw_box(1, 1, UI_W, UI_HEADER_H, title, C_CYAN);

    const char* lines[3] = {" ", " ", " "};
    char buf[256];
    buf[0] = 0;

    if (hint_lines && hint_lines[0]) {
        snprintf(buf, sizeof(buf), "%s", hint_lines);
        int li = 0;
        lines[0] = buf;
        for (char* c = buf; *c && li < 2; c++) {
            if (*c == '\n') {
                *c = 0;
                li++;
                lines[li] = c + 1;
            }
        }
        for (int i = 0; i < 3; i++) if (!lines[i] || !lines[i][0]) lines[i] = " ";
    }

    for (int i = 0; i < 3; i++) ui_print_fit(2 + i, 3, UI_INNER, C_GRAY, "%s", lines[i]);
}

/* --------------------------------------------------------------------------
   Forward UI
----------------------------------------------------------------------------*/
static void ui_log(PadState* pad);
static void ui_settings(PadState* pad);
static void ui_help(PadState* pad);

static bool ui_confirm_cancel(PadState* pad, const char* what) {
redraw:
    char title[64];
    snprintf(title, sizeof(title), "%s", what ? what : "Cancel");

    ui_draw_header(title, "A: Confirm cancel\nB: Resume\nY: Log   ZL: Help");
    ui_draw_box(1, UI_CONTENT_Y, UI_W, UI_CONTENT_H, "Confirm", C_CYAN);

    ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, C_WHITE, "Cancel the current operation?");
    ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Progress will be lost.");

    while (appletMainLoop()) {
        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_Y) { ui_log(pad); goto redraw; }
        if (down & HidNpadButton_ZL) { ui_help(pad); goto redraw; }
        if (down & HidNpadButton_A) return true;
        if (down & HidNpadButton_B) return false;
        consoleUpdate(NULL);
    }
    return true;
}

static void ui_message_screen(PadState* pad, const char* title, const char* msg, const char* hint) {
redraw:
    ui_draw_header(title, hint ? hint : "B or +: Back\nY: Log   ZL: Help\n ");
    ui_draw_box(1, UI_CONTENT_Y, UI_W, UI_CONTENT_H, "Message", C_CYAN);

    int row = UI_CONTENT_Y + 2;
    if (msg) {
        const int w = UI_INNER;
        const char* p = msg;
        while (*p && row < UI_H) {
            char line[96];
            int n = 0;
            while (p[n] && n < w) n++;
            int cut = n;
            if (p[cut] && cut > 0) {
                int s = cut;
                while (s > 0 && p[s] != ' ') s--;
                if (s > 10) cut = s;
            }
            snprintf(line, sizeof(line), "%.*s", cut, p);
            ui_print_fit(row++, 3, UI_INNER, C_WHITE, "%s", line);
            p += cut;
            while (*p == ' ') p++;
        }
    }

    while (appletMainLoop()) {
        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_Y) { ui_log(pad); goto redraw; }
        if (down & HidNpadButton_ZL) { ui_help(pad); goto redraw; }
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) break;
        consoleUpdate(NULL);
    }
}

/* --------------------------------------------------------------------------
   File-system helpers
----------------------------------------------------------------------------*/
typedef struct {
    uint64_t total;
    uint64_t free;
    uint64_t used;
} SpaceInfo;

static bool get_sd_space(SpaceInfo* out) {
    if (!out) return false;
    struct statvfs vfs;
    if (statvfs("sdmc:/", &vfs) != 0) {
        log_pushf("ERROR", "Space query (statvfs) failed: %s", strerror(errno));
        return false;
    }
    uint64_t fr = (uint64_t)(vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize);
    uint64_t blocks = (uint64_t)vfs.f_blocks;
    uint64_t bfree  = (uint64_t)vfs.f_bfree;
    out->total = blocks * fr;
    out->free  = bfree  * fr;
    out->used  = (out->total > out->free) ? (out->total - out->free) : 0;
    return true;
}

static bool quick_rw_test(void) {
    const char* path = "sdmc:/_sdcheck_tmp.bin";
    uint8_t wbuf[4096];
    uint8_t rbuf[4096];
    const size_t sz = sizeof(wbuf);

    for (size_t i = 0; i < sz; i++) wbuf[i] = (uint8_t)(i & 0xFF);

    FILE* f = fopen(path, "wb");
    if (!f) { log_pushf("ERROR", "Write test: fopen(wb) failed: %s", strerror(errno)); return false; }

    size_t w = fwrite(wbuf, 1, sz, f);
    if (w != sz) {
        log_pushf("ERROR", "Write test: fwrite failed (%zu/%zu): %s", w, sz, strerror(errno));
        fclose(f); remove(path); return false;
    }

    fflush(f);
    fclose(f);

    f = fopen(path, "rb");
    if (!f) { log_pushf("ERROR", "Write test: fopen(rb) failed: %s", strerror(errno)); remove(path); return false; }

    size_t r = fread(rbuf, 1, sz, f);
    fclose(f);

    if (r != sz) { log_pushf("ERROR", "Write test: fread failed (%zu/%zu): %s", r, sz, strerror(errno)); remove(path); return false; }
    if (memcmp(wbuf, rbuf, sz) != 0) { log_push("ERROR", "Write test: data mismatch"); remove(path); return false; }

    remove(path);
    return true;
}

/* --------------------------------------------------------------------------
   Log export (single file)
----------------------------------------------------------------------------*/
static void log_write_header(FILE* f, const ScanConfig* cfg) {
    if (!f) return;

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);

    fprintf(f, "SD Check Log\n");
    fprintf(f, "Version: %s  (build %s %s)\n", SDCHECK_VERSION, __DATE__, __TIME__);
    fprintf(f, "Exported: %04d-%02d-%02d %02d:%02d:%02d\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    fprintf(f, "Context: %s\n", log_get_context());

    if (cfg) {
        fprintf(f, "Preset: %s\n", preset_name(cfg->preset));
        fprintf(f, "Settings: Full read=%s, Large-file threshold=%llu MiB, Retries=%d, Consistency=%s, Chunk=%s\n",
                cfg->full_read ? "ON" : "OFF",
                (unsigned long long)(cfg->large_file_limit / (1024ull * 1024ull)),
                cfg->read_retries,
                cfg->consistency_check ? "ON" : "OFF",
                chunk_name(cfg->chunk_mode));
        fprintf(f, "Filters: Skip known folders=%s, Skip media extensions=%s\n",
                cfg->skip_known_folders ? "ON" : "OFF",
                cfg->skip_media_exts ? "ON" : "OFF");
        fprintf(f, "Quick: write test=%s, root listing=%s\n",
                cfg->write_test ? "ON" : "OFF",
                cfg->list_root ? "ON" : "OFF");
    }

    if (g_sleep.inited) {
        fprintf(f, "Auto-sleep: %s  (set_rc=0x%08X, get_rc=0x%08X)\n",
                g_sleep.is_disabled ? "DISABLED" : "ENABLED",
                (unsigned int)g_sleep.rc_set_disable, (unsigned int)g_sleep.rc_get_after);
    } else {
        fprintf(f, "Auto-sleep: (not initialized)\n");
    }

    fprintf(f, "Note: This file is overwritten on each save (sdmc:/sdcheck.log).\n");
    fprintf(f, "------------------------------------------------------------\n");
}

static bool log_export_to_file(const char* path, const ScanConfig* cfg) {
    if (!path) return false;
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    log_write_header(f, cfg);

    int available = log_ring_count();
    for (int i = 0; i < available; i++) {
        const char* line = log_ring_line(i);
		if (line && line[0]) fprintf(f, "%s\n", line);
    }

    fclose(f);
    return true;
}

static bool log_save_to_sdroot(const ScanConfig* cfg) {
    if (access("sdmc:/", F_OK) != 0) {
        log_save_status_set(false, "sdmc:/ not accessible");
        log_push("ERROR", "sdmc:/ is not accessible. Cannot save sdcheck.log");
        return false;
    }

    errno = 0;
    bool ok = log_export_to_file(log_file_path(), cfg);
    int e = errno;

    if (ok) {
        log_save_status_set(true, "OK");
        log_push("INFO", "Log saved to sdmc:/sdcheck.log");
    } else {
        const char* why = (e != 0) ? strerror(e) : "unknown error";
        log_save_status_set(false, why);
        log_pushf("ERROR", "Failed to write sdmc:/sdcheck.log: %s", why);
    }
    return ok;
}

static void get_deep_root(const ScanConfig* cfg, char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    if (!cfg) { snprintf(out, out_sz, "sdmc:/"); return; }

    switch (cfg->deep_target) {
        case SCAN_TARGET_NINTENDO:   snprintf(out, out_sz, "sdmc:/Nintendo"); break;
        case SCAN_TARGET_EMUMMC:     snprintf(out, out_sz, "sdmc:/emuMMC"); break;
        case SCAN_TARGET_SWITCH:     snprintf(out, out_sz, "sdmc:/switch"); break;
        case SCAN_TARGET_CUSTOM_CFG:
            if (cfg->custom_root[0]) snprintf(out, out_sz, "%s", cfg->custom_root);
            else snprintf(out, out_sz, "sdmc:/");
            break;
        default:
            snprintf(out, out_sz, "sdmc:/");
            break;
    }
    /* trim trailing slashes except sdmc:/ */
    const char* prefix = "sdmc:/";
    size_t n = strlen(out);
    while (n > strlen(prefix) && out[n-1] == '/') { out[n-1] = 0; n--; }
}

/* --------------------------------------------------------------------------
   Deep UI
----------------------------------------------------------------------------*/
static void deep_ui_draw_frame(bool paused) {
    if (paused) {
        ui_draw_header("Deep Check (Paused)",
                       "A/X: Resume        Y: Log\n"
                       "Hold B/+/-: Cancel ZL: Help\n"
                       " ");
    } else {
        ui_draw_header("Deep Check",
                       "X: Pause           Y: Log (pause)\n"
                       "Hold B/+/-: Cancel ZL: Help\n"
                       " ");
    }

    if (g_ui.compact_mode) {
        ui_draw_box(1, UI_CONTENT_Y,      UI_W, 8, "Status", C_CYAN);
        ui_draw_box(1, UI_CONTENT_Y + 8,  UI_W, 8, "Current File", C_CYAN);
        ui_draw_box(1, UI_CONTENT_Y + 16, UI_W, 6, "Recent Errors", C_CYAN);
    } else {
        ui_draw_box(1, UI_CONTENT_Y,      UI_W, 7, "Status", C_CYAN);
        ui_draw_box(1, UI_CONTENT_Y + 7,  UI_W, 7, "Current File", C_CYAN);
        ui_draw_box(1, UI_CONTENT_Y + 14, UI_W, 4, "System", C_CYAN);
        ui_draw_box(1, UI_CONTENT_Y + 18, UI_W, 5, "Recent Errors", C_CYAN);
    }
}

static void deep_ui_pause_modal(ScanStats* st, PadState* pad) {
    if (!st || !pad) return;

    deep_ui_draw_frame(true);
    st->ui_drawn = true;
    st->ui_last_ms = 0;

    /* keep showing a minimal state while waiting */
    while (appletMainLoop() && st->paused && !st->cancelled) {
        uint64_t down = poll_down(pad);
        uint64_t held = padGetButtons(pad);

        if (down & HidNpadButton_Y) {
            ui_log(pad);
            ui_wait_release(pad, HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus, 1500);
            deep_ui_draw_frame(true);
        }

        if (down & HidNpadButton_ZL) {
            ui_help(pad);
            deep_ui_draw_frame(true);
        }

        if (down & (HidNpadButton_A | HidNpadButton_X)) {
            st->paused = false;
            if (st->pause_start_ms) st->paused_total_ms += (now_ms() - st->pause_start_ms);
            st->pause_start_ms = 0;
            log_push("INFO", "Deep Check resumed.");
            deep_ui_draw_frame(false);
            st->ui_drawn = true;
            st->ui_last_ms = 0;
            ui_wait_release(pad, HidNpadButton_A | HidNpadButton_X, 500);
            break;
        }

        const uint64_t cancel_mask = HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus;
        if (held & cancel_mask) {
            uint64_t now = now_ms();
            if (st->cancel_hold_start_ms == 0) {
                st->cancel_hold_start_ms = now;
            } else if (!st->cancel_prompt_active && (now - st->cancel_hold_start_ms) >= 650) {
                st->cancel_prompt_active = true;
                bool ok = ui_confirm_cancel(pad, "Deep Check");
                ui_wait_release(pad, cancel_mask, 1500);
                st->cancel_hold_start_ms = 0;
                st->cancel_prompt_active = false;
                if (ok) { st->cancelled = true; break; }
                deep_ui_draw_frame(true);
            }
        } else {
            st->cancel_hold_start_ms = 0;
            st->cancel_prompt_active = false;
        }

        /* minimal paused screen content */
        ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, C_WHITE, "Paused. No data is being read.");
        ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_GRAY,  "Tip: Use Y to view log without cancelling the scan.");

        consoleUpdate(NULL);
        svcSleepThread(40 * 1000 * 1000);
    }
}

static void deep_ui_maybe_update(ScanStats* st, PadState* pad, bool force) {
    if (!st || !st->ui_active) return;

    uint64_t now = now_ms();
    if (!appletMainLoop()) { st->cancelled = true; return; }

    if (pad && (now - st->input_last_ms) >= 40) {
        padUpdate(pad);
        uint64_t down = padGetButtonsDown(pad);
        uint64_t held = padGetButtons(pad);

        if (down & HidNpadButton_ZL) {
            /* Help pauses the scan */
            ui_help(pad);
            deep_ui_draw_frame(st->paused);
            st->ui_drawn = true;
            st->ui_last_ms = 0;
            st->input_last_ms = now;
            return;
        }

        if (down & HidNpadButton_Y) {
            ui_log(pad);
            ui_wait_release(pad, HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus, 1500);
            st->cancel_hold_start_ms = 0;
            st->cancel_prompt_active = false;
            deep_ui_draw_frame(st->paused);
            st->ui_drawn = true;
            st->ui_last_ms = 0;
            st->input_last_ms = now;
            return;
        }

        if (!st->paused && (down & HidNpadButton_X)) {
            st->paused = true;
            st->pause_start_ms = now;
            log_push("INFO", "Deep Check paused.");
            deep_ui_pause_modal(st, pad);
            st->input_last_ms = now_ms();
            return;
        }

        /* Cancel (hold + confirm) */
        const uint64_t cancel_mask = HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus;
        if (held & cancel_mask) {
            if (st->cancel_hold_start_ms == 0) {
                st->cancel_hold_start_ms = now;
            } else if (!st->cancel_prompt_active && (now - st->cancel_hold_start_ms) >= 650) {
                st->cancel_prompt_active = true;
                bool ok = ui_confirm_cancel(pad, "Deep Check");
                ui_wait_release(pad, cancel_mask, 1500);

                st->cancel_hold_start_ms = 0;
                st->cancel_prompt_active = false;

                if (ok) { st->cancelled = true; st->input_last_ms = now; return; }

                deep_ui_draw_frame(false);
                st->ui_drawn = true;
                st->ui_last_ms = 0;
                st->input_last_ms = now;
                return;
            }
        } else {
            st->cancel_hold_start_ms = 0;
            st->cancel_prompt_active = false;
        }

        st->input_last_ms = now;
    }

    if (st->paused) {
        deep_ui_pause_modal(st, pad);
        return;
    }

    if (!force && st->ui_last_ms && (now - st->ui_last_ms) < 250) return;

    if (!st->ui_drawn) {
        deep_ui_draw_frame(false);
        st->ui_drawn = true;
    }

    if (!st->speed_last_ms) {
        st->speed_last_ms = now;
        st->speed_last_bytes = st->bytes_read;
        st->speed_mib_s = 0.0;
    } else if ((now - st->speed_last_ms) >= 500) {
        uint64_t dt = now - st->speed_last_ms;
        uint64_t db = st->bytes_read - st->speed_last_bytes;
        double secs = (double)dt / 1000.0;
        st->speed_mib_s = (secs > 0.0) ? ((double)db / 1048576.0 / secs) : 0.0;
        st->speed_last_ms = now;
        st->speed_last_bytes = st->bytes_read;
    }

    char elapsed[32];
    format_hms(elapsed, sizeof(elapsed), scan_stats_elapsed_ms(st, now));

    char total_read[32];
    format_bytes(total_read, sizeof(total_read), st->bytes_read);

    char sz[32];
    format_bytes(sz, sizeof(sz), st->current_size);

    char rd[32];
    format_bytes(rd, sizeof(rd), st->current_done);

    uint64_t planned = st->current_planned;
    uint64_t done = st->current_done;
    int pct = 0;
    if (planned == 0) pct = 100;
    else {
        pct = (int)((done * 100ULL) / planned);
        if (pct > 100) pct = 100;
        if (pct < 0) pct = 0;
    }

    char bar[41];
    int barw = (int)sizeof(bar) - 1;
    int fill = (pct * barw) / 100;
    if (fill < 0) fill = 0;
    if (fill > barw) fill = barw;
    for (int i = 0; i < barw; i++) bar[i] = (i < fill) ? '#' : '.';
    bar[barw] = 0;

    char path_disp[80];
    tail_ellipsize(path_disp, sizeof(path_disp), st->current_path[0] ? st->current_path : "(none)", 72);

    if (g_ui.compact_mode) {
        int sy = UI_CONTENT_Y + 1;
        ui_print_fit(sy + 0, 3, UI_INNER, C_WHITE,  "Start: %-12s  Elapsed: %s", st->wall_start_str, elapsed);
        ui_print_fit(sy + 1, 3, UI_INNER, C_WHITE,  "Speed: %6.2f MiB/s  Read: %-12s", st->speed_mib_s, total_read);
        ui_print_fit(sy + 2, 3, UI_INNER, C_WHITE,  "Dirs: %-8llu  Files read/total: %llu/%llu",
                     (unsigned long long)st->dirs_total,
                     (unsigned long long)st->files_read,
                     (unsigned long long)st->files_total);
        const char* vcol = (st->read_errors || st->consistency_errors) ? C_RED : ((st->open_errors || st->stat_errors || st->path_errors) ? C_YELLOW : C_GREEN);
        ui_print_fit(sy + 3, 3, UI_INNER, vcol, "Errors: read=%llu  open=%llu  stat=%llu  path=%llu  consistency=%llu",
                     (unsigned long long)st->read_errors,
                     (unsigned long long)st->open_errors,
                     (unsigned long long)st->stat_errors,
                     (unsigned long long)st->path_errors,
                     (unsigned long long)st->consistency_errors);
        ui_print_fit(sy + 4, 3, UI_INNER, C_GRAY,  "Transient read errors (recovered): %llu", (unsigned long long)st->read_errors_transient);
        ui_print_fit(sy + 5, 3, UI_INNER, C_GRAY,  "Skipped: %llu dirs, %llu files", (unsigned long long)st->skipped_dirs, (unsigned long long)st->skipped_files);
        ui_print_fit(sy + 6, 3, UI_INNER, C_GRAY,  "Policy: full=%s  threshold=%llu MiB  retries=%d  consistency=%s",
                     st->run_full_read ? "ON" : "OFF",
                     (unsigned long long)(st->run_large_limit / (1024ull*1024ull)),
                     st->run_retries,
                     st->run_consistency ? "ON" : "OFF");

        int fy = UI_CONTENT_Y + 8 + 1;
        ui_print_fit(fy + 0, 3, UI_INNER, C_WHITE, "File: %-72s", path_disp);
        const char* mode_col = st->current_sample ? C_YELLOW : C_GREEN;
        ui_print_fit(fy + 1, 3, UI_INNER, mode_col, "Mode: %-6s  Size: %-12s", st->current_sample ? "SAMPLE" : "FULL", sz);
        char pl[32];
        format_bytes(pl, sizeof(pl), planned);
        ui_print_fit(fy + 2, 3, UI_INNER, C_WHITE, "Read : %-12s / %-12s  (%3d%%)", rd, pl, pct);
        ui_print_fit(fy + 3, 3, UI_INNER, C_WHITE, "[%-40s]", bar);
        ui_print_fit(fy + 4, 3, UI_INNER, C_DIM,   " ");

        int ey = UI_CONTENT_Y + 16 + 2;
        int shown = st->err_ring_count;
        if (shown > 3) shown = 3;
        if (shown <= 0) {
            ui_print_fit(ey + 0, 3, UI_INNER, C_GREEN, "No errors logged.");
            ui_print_fit(ey + 1, 3, UI_INNER, C_DIM, " ");
            ui_print_fit(ey + 2, 3, UI_INNER, C_DIM, " ");
        } else {
            for (int i = 0; i < 3; i++) {
                int row = ey + i;
                if (i >= shown) { ui_print_fit(row, 3, UI_INNER, C_DIM, " "); continue; }
                int idx = (st->err_ring_count - 1 - i) % ERR_RING_MAX;
                ui_print_fit(row, 3, UI_INNER, C_RED, "%s", st->err_ring[idx]);
            }
        }

    } else {
        int sy = UI_CONTENT_Y + 1;
        ui_print_fit(sy + 0, 3, UI_INNER, C_WHITE,  "Start: %-12s   Elapsed: %s", st->wall_start_str, elapsed);
        ui_print_fit(sy + 1, 3, UI_INNER, C_WHITE,  "Speed: %6.2f MiB/s   Read: %-12s", st->speed_mib_s, total_read);
        ui_print_fit(sy + 2, 3, UI_INNER, C_WHITE,  "Dirs: %-8llu   Files read/total: %llu/%llu",
                     (unsigned long long)st->dirs_total,
                     (unsigned long long)st->files_read,
                     (unsigned long long)st->files_total);
        const char* err_col = (st->read_errors || st->consistency_errors) ? C_RED : ((st->open_errors || st->stat_errors || st->path_errors) ? C_YELLOW : C_GREEN);
        ui_print_fit(sy + 3, 3, UI_INNER, err_col, "Errors: read=%llu (transient %llu)  open=%llu  stat=%llu  path=%llu  consistency=%llu",
                     (unsigned long long)st->read_errors,
                     (unsigned long long)st->read_errors_transient,
                     (unsigned long long)st->open_errors,
                     (unsigned long long)st->stat_errors,
                     (unsigned long long)st->path_errors,
                     (unsigned long long)st->consistency_errors);
        ui_print_fit(sy + 4, 3, UI_INNER, C_GRAY,  "Policy: full=%s  threshold=%llu MiB  retries=%d  consistency=%s",
                     st->run_full_read ? "ON" : "OFF",
                     (unsigned long long)(st->run_large_limit / (1024ull*1024ull)),
                     st->run_retries,
                     st->run_consistency ? "ON" : "OFF");

        int fy = UI_CONTENT_Y + 7 + 1;
        ui_print_fit(fy + 0, 3, UI_INNER, C_WHITE, "File: %-72s", path_disp);
        const char* mode_col = st->current_sample ? C_YELLOW : C_GREEN;
        ui_print_fit(fy + 1, 3, UI_INNER, mode_col, "Mode: %-6s  Size: %-12s", st->current_sample ? "SAMPLE" : "FULL", sz);
        char pl[32];
        format_bytes(pl, sizeof(pl), planned);
        ui_print_fit(fy + 2, 3, UI_INNER, C_WHITE, "Read : %-12s / %-12s   (%3d%%)", rd, pl, pct);
        ui_print_fit(fy + 3, 3, UI_INNER, C_WHITE, "[%-40s]", bar);
        ui_print_fit(fy + 4, 3, UI_INNER, C_DIM, " ");

        int sysy = UI_CONTENT_Y + 14 + 1;
        const char* sleep_col = C_YELLOW;
        const char* sleep_state = "UNKNOWN";
        if (g_sleep.inited) {
            if (R_SUCCEEDED(g_sleep.rc_set_disable) && R_SUCCEEDED(g_sleep.rc_get_after) && g_sleep.is_disabled) {
                sleep_col = C_GREEN;
                sleep_state = "DISABLED (OK)";
            } else if (R_FAILED(g_sleep.rc_set_disable)) {
                sleep_col = C_RED;
                sleep_state = "DISABLE FAILED";
            } else {
                sleep_col = C_YELLOW;
                sleep_state = g_sleep.is_disabled ? "DISABLED (UNVERIFIED)" : "ENABLED";
            }
        } else {
            sleep_col = C_YELLOW;
            sleep_state = "NOT INITIALIZED";
        }
        ui_print_fit(sysy + 0, 3, UI_INNER, sleep_col, "Auto-Sleep: %s", sleep_state);
        ui_print_fit(sysy + 1, 3, UI_INNER, C_GRAY,   "Skipped: %llu dirs, %llu files", (unsigned long long)st->skipped_dirs, (unsigned long long)st->skipped_files);

        int ey = UI_CONTENT_Y + 18 + 2;
        int shown = st->err_ring_count;
        if (shown > 3) shown = 3;
        if (shown <= 0) {
            ui_print_fit(ey + 0, 3, UI_INNER, C_GREEN, "No errors logged.");
            ui_print_fit(ey + 1, 3, UI_INNER, C_DIM, " ");
            ui_print_fit(ey + 2, 3, UI_INNER, C_DIM, " ");
        } else {
            for (int i = 0; i < 3; i++) {
                int row = ey + i;
                if (i >= shown) { ui_print_fit(row, 3, UI_INNER, C_DIM, " "); continue; }
                int idx = (st->err_ring_count - 1 - i) % ERR_RING_MAX;
                ui_print_fit(row, 3, UI_INNER, C_RED, "%s", st->err_ring[idx]);
            }
        }
    }

    st->ui_last_ms = now;
    consoleUpdate(NULL);
}

/* --------------------------------------------------------------------------
   Results / Summary
----------------------------------------------------------------------------*/
typedef enum {
    VERDICT_PASSED = 0,
    VERDICT_WARNINGS,
    VERDICT_FAILED,
    VERDICT_CANCELLED
} Verdict;

static const char* verdict_name(Verdict v) {
    switch (v) {
        case VERDICT_FAILED: return "FAILED";
        case VERDICT_WARNINGS: return "WARNINGS";
        case VERDICT_CANCELLED: return "CANCELLED";
        default: return "PASSED";
    }
}

static const char* verdict_color(Verdict v) {
    switch (v) {
        case VERDICT_FAILED: return C_RED;
        case VERDICT_WARNINGS: return C_YELLOW;
        case VERDICT_CANCELLED: return C_YELLOW;
        default: return C_GREEN;
    }
}

typedef struct {
    bool ran;
    bool cancelled;

    uint64_t dirs_total;
    uint64_t files_total;
    uint64_t files_read;

    uint64_t bytes_read;
    double seconds;

    uint64_t open_errors;
    uint64_t read_errors;
    uint64_t read_errors_transient;
    uint64_t stat_errors;
    uint64_t path_errors;
    uint64_t consistency_errors;

    uint64_t skipped_dirs;
    uint64_t skipped_files;

    /* quick specific */
    bool sd_accessible;
    bool space_ok;
    bool root_ok;
    bool write_test_enabled;
    bool write_test_ok;

    bool log_saved;
    bool log_save_ok;

    SpaceInfo space;

    /* report */
    Verdict verdict;



    /* perf (Deep Check only) */
    uint64_t perf_ops;
    uint64_t perf_bytes;
    uint64_t perf_hist[5];
    uint64_t perf_stalls;
    uint64_t perf_stall_total_ms;
    uint64_t perf_longest_ms;
    double   perf_longest_mib_s;
    uint64_t perf_longest_off;
    uint64_t perf_longest_bytes;
    char     perf_longest_path[256];

    /* first failure context (Deep Check only) */
    bool     first_fail_set;
    char     first_fail_kind[16];
    char     first_fail_path[256];
    uint64_t first_fail_off;
    uint64_t first_fail_bytes;
    int      first_fail_errno;
    char     first_fail_note[96];
    /* deep details */
    LargestEntry largest[LARGEST_MAX];
    int largest_count;

    char fail_paths[FAIL_MAX][256];
    int fail_count;

    ScanConfig effective_cfg;
} RunResult;

static void runresult_clear(RunResult* r) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
}

static Verdict compute_verdict(const RunResult* r) {
    if (!r || !r->ran) return VERDICT_WARNINGS;
    if (r->cancelled) return VERDICT_CANCELLED;
    if (r->read_errors > 0 || r->consistency_errors > 0) return VERDICT_FAILED;
    if (r->write_test_enabled && !r->write_test_ok) return VERDICT_FAILED;

    bool any_warn = false;
    if (r->open_errors || r->stat_errors || r->path_errors) any_warn = true;
    if (r->read_errors_transient) any_warn = true;
    if (r->skipped_dirs || r->skipped_files) any_warn = true;
    if (!r->write_test_enabled) any_warn = true; /* no write validation */

    return any_warn ? VERDICT_WARNINGS : VERDICT_PASSED;
}

static void build_next_steps(const RunResult* r, char out[4][96]) {
    for (int i = 0; i < 4; i++) snprintf(out[i], 96, " ");
    if (!r) return;

    if (r->cancelled) {
        snprintf(out[0], 96, "- Scan was cancelled. Re-run for full coverage.");
        return;
    }

    if (r->read_errors > 0 || r->consistency_errors > 0) {
        snprintf(out[0], 96, "- Back up important data immediately.");
        snprintf(out[1], 96, "- Test the SD on a PC (full surface read). Replace if errors repeat.");
        snprintf(out[2], 96, "- If filesystem is corrupted, copy off data, format, and restore.");
        return;
    }

    if (r->open_errors || r->stat_errors || r->path_errors) {
        snprintf(out[0], 96, "- No read errors, but metadata/access issues were detected.");
        snprintf(out[1], 96, "- Run a filesystem check on a PC (chkdsk/fsck).\n");
        snprintf(out[2], 96, "- Watch for path length issues or permissions from homebrew tools.");
        return;
    }

    if (r->read_errors_transient) {
        snprintf(out[0], 96, "- Some transient read errors recovered by retry.");
        snprintf(out[1], 96, "- Consider a full re-test; intermittent I/O can indicate a degrading card.");
        return;
    }

    if (r->skipped_dirs || r->skipped_files) {
        snprintf(out[0], 96, "- Some items were skipped by policy filters.");
        snprintf(out[1], 96, "- Use Preset: Forensics or disable filters for full coverage.");
        return;
    }

    snprintf(out[0], 96, "- No issues detected. If you suspect problems, run Forensics preset.");
}

static void ui_results_draw(const char* title, const RunResult* r) {
    ui_draw_header(title ? title : "Results",
                   "B/+ : Back    X: Settings    R: Summary\n"
                   "Y: Log        ZL: Help\n"
                   " ");

    ui_draw_box(1, UI_CONTENT_Y, UI_W, 11, "Summary", C_CYAN);

    Verdict v = r ? r->verdict : VERDICT_WARNINGS;
    ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, verdict_color(v), "Verdict: %s", verdict_name(v));

    char br[32];
    format_bytes(br, sizeof(br), r ? r->bytes_read : 0);

    ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Dirs: %-8llu   Files read/total: %llu/%llu",
                 (unsigned long long)(r ? r->dirs_total : 0),
                 (unsigned long long)(r ? r->files_read : 0),
                 (unsigned long long)(r ? r->files_total : 0));
    ui_print_fit(UI_CONTENT_Y + 4, 3, UI_INNER, C_WHITE, "Read: %-12s   Time: %.1f s", br, r ? r->seconds : 0.0);

    if (r) {
        ui_print_fit(UI_CONTENT_Y + 5, 3, UI_INNER, (v==VERDICT_FAILED)?C_RED:((v==VERDICT_WARNINGS)?C_YELLOW:C_GREEN),
                     "Errors: read=%llu (transient %llu)  open=%llu  stat=%llu  path=%llu  consistency=%llu",
                     (unsigned long long)r->read_errors,
                     (unsigned long long)r->read_errors_transient,
                     (unsigned long long)r->open_errors,
                     (unsigned long long)r->stat_errors,
                     (unsigned long long)r->path_errors,
                     (unsigned long long)r->consistency_errors);
        ui_print_fit(UI_CONTENT_Y + 6, 3, UI_INNER, C_GRAY,
                     "Skipped: %llu dirs, %llu files    Preset: %s",
                     (unsigned long long)r->skipped_dirs,
                     (unsigned long long)r->skipped_files,
                     preset_name(r->effective_cfg.preset));
    }

    ui_draw_box(1, 17, UI_W, 12, "Details / Next steps", C_CYAN);

    int row = 19;
    if (r && r->ran) {
        if (r->write_test_enabled) {
            ui_print_fit(row++, 3, UI_INNER, r->write_test_ok ? C_GREEN : C_RED, "Quick write test: %s", r->write_test_ok ? "OK" : "FAILED");
        } else {
            ui_print_fit(row++, 3, UI_INNER, C_YELLOW, "Quick write test: OFF (no write validation performed)");
        }

        if (r->space_ok) {
            char t[32], u[32], f[32];
            format_bytes(t, sizeof(t), r->space.total);
            format_bytes(u, sizeof(u), r->space.used);
            format_bytes(f, sizeof(f), r->space.free);
            ui_print_fit(row++, 3, UI_INNER, C_WHITE, "SD space: total %s   used %s   free %s", t, u, f);
        } else {
            ui_print_fit(row++, 3, UI_INNER, C_YELLOW, "SD space: unavailable");
        }

        ui_print_fit(row++, 3, UI_INNER, C_WHITE, "Auto-sleep: %s", (g_sleep.inited && g_sleep.is_disabled) ? "DISABLED" : "ENABLED/UNKNOWN");
    }

    if (r && r->log_saved) {
        ui_print_fit(row++, 3, UI_INNER, r->log_save_ok ? C_GREEN : C_YELLOW,
                     "Log file: sdmc:/sdcheck.log (%s)", r->log_save_ok ? "saved" : "save failed");
    } else {
        ui_print_fit(row++, 3, UI_INNER, C_GRAY, "Log file: sdmc:/sdcheck.log (not saved)");
    }

    if (r) {
        char steps[4][96];
        build_next_steps(r, steps);
        for (int i = 0; i < 4; i++) {
            if (steps[i][0] && strcmp(steps[i], " ") != 0) ui_print_fit(row++, 3, UI_INNER, C_WHITE, "%s", steps[i]);
        }
    }

    if (row < 28) ui_print_fit(27, 3, UI_INNER, C_GRAY, "Tip: For full coverage, use Preset: Forensics.");
}


static void ui_summary_draw(const RunResult* r, int page) {
    if (page < 0) page = 0;
    if (page > 1) page = 1;

    char hint[256];
    snprintf(hint, sizeof(hint),
             "B/+ : Back    Y: Log    L/R: Page (%d/2)\n"
             "ZL: Help\n"
             " ", page + 1);

    ui_draw_header("Summary", hint);
    /* Page 1: Run + Performance + First failure */
    if (page == 0) {
        ui_draw_box(1, UI_CONTENT_Y, UI_W, 8, "Run", C_CYAN);

        Verdict v = r ? r->verdict : VERDICT_WARNINGS;
        ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, verdict_color(v), "Verdict: %s", verdict_name(v));

        if (r) {
            char br[32];
            format_bytes(br, sizeof(br), r->bytes_read);
            ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Dirs: %-8llu   Files read/total: %llu/%llu",
                         (unsigned long long)r->dirs_total,
                         (unsigned long long)r->files_read,
                         (unsigned long long)r->files_total);
            ui_print_fit(UI_CONTENT_Y + 4, 3, UI_INNER, C_WHITE, "Read: %-12s   Time: %.1f s", br, r->seconds);
            ui_print_fit(UI_CONTENT_Y + 5, 3, UI_INNER, C_WHITE, "Preset: %-9s   Full read: %-3s   Threshold: %llu MiB",
                         preset_name(r->effective_cfg.preset), onoff(r->effective_cfg.full_read),
                         (unsigned long long)(r->effective_cfg.large_file_limit/(1024ull*1024ull)));
            ui_print_fit(UI_CONTENT_Y + 6, 3, UI_INNER, C_WHITE, "Retries: %d   Consistency: %s   Filters: folders=%s exts=%s",
                         r->effective_cfg.read_retries, onoff(r->effective_cfg.consistency_check),
                         onoff(r->effective_cfg.skip_known_folders), onoff(r->effective_cfg.skip_media_exts));
        }

        ui_draw_box(1, UI_CONTENT_Y + 8, UI_W, 8, "Performance (MiB/s)", C_CYAN);
        int row = UI_CONTENT_Y + 10;
        if (r && r->perf_ops > 0 && r->seconds > 0.0) {
            double avg = ((double)r->perf_bytes / 1048576.0) / r->seconds;
            ui_print_fit(row++, 3, UI_INNER, C_WHITE, "Avg throughput: %.2f MiB/s   Ops: %llu   Bytes: %.2f MiB",
                         avg,
                         (unsigned long long)r->perf_ops,
                         (double)r->perf_bytes / 1048576.0);

            ui_print_fit(row++, 3, UI_INNER, C_WHITE,
                         "Buckets (ops): >=60:%llu  30-60:%llu  10-30:%llu  1-10:%llu  <1:%llu",
                         (unsigned long long)r->perf_hist[0],
                         (unsigned long long)r->perf_hist[1],
                         (unsigned long long)r->perf_hist[2],
                         (unsigned long long)r->perf_hist[3],
                         (unsigned long long)r->perf_hist[4]);

            ui_print_fit(row++, 3, UI_INNER, C_WHITE,
                         "Stalls: %llu (%llu ms)   Longest op: %llu ms @ %.2f MiB/s",
                         (unsigned long long)r->perf_stalls,
                         (unsigned long long)r->perf_stall_total_ms,
                         (unsigned long long)r->perf_longest_ms,
                         r->perf_longest_mib_s);

            char disp[80];
            tail_ellipsize(disp, sizeof(disp), r->perf_longest_path[0] ? r->perf_longest_path : "(unknown)", 72);
            ui_print_fit(row++, 3, UI_INNER, C_GRAY, "Longest path: %s", disp);
        } else {
            ui_print_fit(row++, 3, UI_INNER, C_GRAY, "(No performance data. Quick Check does not collect per-op read speeds.)");
        }

        ui_draw_box(1, UI_CONTENT_Y + 16, UI_W, 12, "First failure (context)", C_CYAN);
        row = UI_CONTENT_Y + 18;
        if (r && r->first_fail_set) {
            ui_print_fit(row++, 3, UI_INNER, C_RED, "Kind: %s   errno: %d   off: %llu   bytes: %llu",
                         r->first_fail_kind,
                         r->first_fail_errno,
                         (unsigned long long)r->first_fail_off,
                         (unsigned long long)r->first_fail_bytes);
            if (r->first_fail_note[0]) ui_print_fit(row++, 3, UI_INNER, C_WHITE, "Note: %s", r->first_fail_note);
            char disp[80];
            tail_ellipsize(disp, sizeof(disp), r->first_fail_path, 72);
            ui_print_fit(row++, 3, UI_INNER, C_WHITE, "Path: %s", disp);
        } else {
            ui_print_fit(row++, 3, UI_INNER, C_GREEN, "No failure context captured.");
        }

        ui_print_fit(27, 3, UI_INNER, C_GRAY, "Tip: If stalls are frequent, test the card on a PC and consider replacing it.");
        return;
    }

    /* Page 2: Failing paths + Largest files */
    ui_draw_box(1, UI_CONTENT_Y, UI_W, 7, "Run", C_CYAN);

    Verdict v = r ? r->verdict : VERDICT_WARNINGS;
    ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, verdict_color(v), "Verdict: %s", verdict_name(v));
    if (r) {
        ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Mode: %s    Preset: %s", r->effective_cfg.full_read ? "Deep" : "Deep/Quick", preset_name(r->effective_cfg.preset));
        ui_print_fit(UI_CONTENT_Y + 4, 3, UI_INNER, C_WHITE, "Full read: %s    Threshold: %llu MiB", onoff(r->effective_cfg.full_read), (unsigned long long)(r->effective_cfg.large_file_limit/(1024ull*1024ull)));
        ui_print_fit(UI_CONTENT_Y + 5, 3, UI_INNER, C_WHITE, "Retries: %d    Consistency: %s    Chunk: %s",
                     r->effective_cfg.read_retries, onoff(r->effective_cfg.consistency_check), chunk_name(r->effective_cfg.chunk_mode));
        ui_print_fit(UI_CONTENT_Y + 6, 3, UI_INNER, C_WHITE, "Filters: skip folders=%s    skip exts=%s",
                     onoff(r->effective_cfg.skip_known_folders), onoff(r->effective_cfg.skip_media_exts));
    }

    ui_draw_box(1, UI_CONTENT_Y + 7, UI_W, 7, "Top failing paths (first 5)", C_CYAN);
    int row = UI_CONTENT_Y + 9;
    if (r && r->fail_count > 0) {
        for (int i = 0; i < r->fail_count && i < 5; i++) {
            char disp[80];
            tail_ellipsize(disp, sizeof(disp), r->fail_paths[i], 72);
            ui_print_fit(row++, 3, UI_INNER, C_RED, "- %s", disp);
        }
    } else {
        ui_print_fit(row++, 3, UI_INNER, C_GREEN, "No failing paths recorded.");
    }

    ui_draw_box(1, UI_CONTENT_Y + 14, UI_W, 14, "Largest files encountered (Top 10)", C_CYAN);
    row = UI_CONTENT_Y + 16;
    if (r && r->largest_count > 0) {
        for (int i = 0; i < r->largest_count && i < 10 && row < UI_H; i++) {
            char sz[24];
            format_bytes(sz, sizeof(sz), r->largest[i].size);
            char disp[80];
            tail_ellipsize(disp, sizeof(disp), r->largest[i].path, 60);
            ui_print_fit(row++, 3, UI_INNER, C_WHITE, "%2d) %-10s  %s", i + 1, sz, disp);
        }
    } else {
        ui_print_fit(row++, 3, UI_INNER, C_GRAY, "(No entries. Quick Check does not enumerate files.)");
    }
}

static void ui_results(PadState* pad, const char* title, RunResult* r) {
    if (r) r->verdict = compute_verdict(r);

    while (appletMainLoop()) {
        ui_results_draw(title, r);
        consoleUpdate(NULL);

        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_Y) { ui_log(pad); }
        if (down & HidNpadButton_ZL) { ui_help(pad); }
        if (down & HidNpadButton_X) { ui_settings(pad); }
        if (down & HidNpadButton_R) {
            int page = 0;
            while (appletMainLoop()) {
                ui_summary_draw(r, page);
                consoleUpdate(NULL);
                uint64_t d2 = poll_down(pad);
                if (d2 & HidNpadButton_Y) { ui_log(pad); continue; }
                if (d2 & HidNpadButton_ZL) { ui_help(pad); continue; }
                if (d2 & HidNpadButton_L) { page = (page == 0) ? 1 : 0; continue; }
                if (d2 & HidNpadButton_R) { page = (page == 0) ? 1 : 0; continue; }
                if (d2 & (HidNpadButton_B | HidNpadButton_Plus)) break;
            }
        }
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return;
    }
}

/* --------------------------------------------------------------------------
   Log UI
----------------------------------------------------------------------------*/
static void ui_log_draw(int scroll, const ScanConfig* cfg) {
    char hint[256];

    const LogSaveStatus* ls = log_save_status();

    char last_hint[128];
    if (ls && ls->known) {
        snprintf(last_hint, sizeof(last_hint), "B/+ : Back     Last saved: %s %s",
                 ls->when_str,
                 ls->ok ? "OK" : "FAILED");
    } else {
        snprintf(last_hint, sizeof(last_hint), "B/+ : Back     Last saved: --");
    }

    snprintf(hint, sizeof(hint),
             "Up/Down: Scroll      L/R: Page\n"
             "A: Save to file     -: Clear     ZL: Help\n"
             "%s",
             last_hint);

    ui_draw_header("Log", hint);
    ui_draw_box(1, UI_CONTENT_Y, UI_W, UI_CONTENT_H, "Recent messages", C_CYAN);

    int available = log_ring_count();

    int status_row = UI_CONTENT_Y + 2;
    int list_row   = UI_CONTENT_Y + 3;

    if (ls && ls->known) {
        if (ls->ok) {
            ui_print_fit(status_row, 3, UI_INNER, C_GREEN, "Log file: %s (saved %s)", log_file_path(), ls->when_str);
        } else {
            ui_print_fit(status_row, 3, UI_INNER, C_RED, "Log file: %s (save failed: %s)",
                         log_file_path(),
                         ls->note[0] ? ls->note : "unknown");
        }
    } else {
        ui_print_fit(status_row, 3, UI_INNER, C_GRAY, "Log file: %s (not saved yet)", log_file_path());
    }

    int max_lines = UI_CONTENT_H - 3;
    if (scroll < 0) scroll = 0;
    if (scroll > available) scroll = available;

    int first = (available - max_lines) - scroll;
    if (first < 0) first = 0;
    int last_idx = first + max_lines;
    if (last_idx > available) last_idx = available;

    for (int i = 0; i < max_lines; i++) {
        int row = list_row + i;
        int idx = first + i;
        if (idx >= last_idx) {
            ui_print_fit(row, 3, UI_INNER, C_DIM, " ");
            continue;
        }

        const char* line = log_ring_line(idx);
        ui_print_fit(row, 3, UI_INNER, C_WHITE, "%s", line ? line : "");
    }

    (void)cfg;
}

static void ui_log(PadState* pad) {
    int scroll = 0;
    while (appletMainLoop()) {
        ui_log_draw(scroll, &g_cfg);
        consoleUpdate(NULL);

        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_ZL) { ui_help(pad); continue; }

        if (down & HidNpadButton_Up) scroll += 1;
        if (down & HidNpadButton_Down) { if (scroll > 0) scroll -= 1; }

        if (down & HidNpadButton_L) scroll += 6;
        if (down & HidNpadButton_R) { scroll -= 6; if (scroll < 0) scroll = 0; }

        if (down & HidNpadButton_A) {
            (void)log_save_to_sdroot(&g_cfg);
            scroll = 0;
        }

        if (down & HidNpadButton_Minus) {
            log_clear();
            scroll = 0;
            log_push("INFO", "Log cleared.");
            errno = 0;
            bool ok = log_export_to_file(log_file_path(), &g_cfg);
            int e = errno;
            if (ok) log_save_status_set(true, "OK");
            else log_save_status_set(false, (e != 0) ? strerror(e) : "unknown error");
        }

        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return;
    }
}

/* --------------------------------------------------------------------------
   Help UI
----------------------------------------------------------------------------*/
static void ui_help(PadState* pad) {
    const char* msg =
        "SD Check is a read-focused diagnostic tool for the SD card.\n\n"
        "What it does:\n"
        "- Quick Check: SD access, space query, optional root listing, optional write test.\n"
        "- Deep Check: reads files to detect I/O/read errors (and optional consistency checks).\n\n"
        "What it does NOT do:\n"
        "- It does not repair the filesystem (no chkdsk/fsck functionality).\n"
        "- Deep Check is read-only. Only the Quick write test can write a tiny temp file.\n\n"
        "Key concepts:\n"
        "- Read errors or consistency mismatches usually indicate a bad/unstable SD card.\n"
        "- Open/stat/path errors can indicate filesystem or metadata issues.\n"
        "- Preset: Forensics disables skips and maximizes coverage.";

    ui_message_screen(pad, "Help", msg, "B/+ : Back\nY: Log\n ");
}

/* --------------------------------------------------------------------------
   Home and Settings
----------------------------------------------------------------------------*/
typedef enum {
    HOME_ACT_NONE = 0,
    HOME_ACT_QUICK,
    HOME_ACT_DEEP,
    HOME_ACT_SETTINGS,
    HOME_ACT_LOG,
    HOME_ACT_EXIT
} HomeAction;

static void ui_home_draw(int sel) {
    ui_draw_header("Home",
                   "Up/Down: Select   A: Start   ZL: Help\n"
                   "X: Settings       -: Reset defaults\n"
                   "Y: Log            +: Exit");

    ui_draw_box(1, UI_CONTENT_Y, UI_W, 7, "Actions", C_CYAN);
    ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, (sel==0)?C_GREEN:C_WHITE, "%s  Quick Check", (sel==0)?">":" ");
    ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, (sel==1)?C_GREEN:C_WHITE, "%s  Deep Check",  (sel==1)?">":" ");

    ui_draw_box(1, 13, UI_W, 7, "Current Settings (saved)", C_CYAN);

    char lim[32];
    snprintf(lim, sizeof(lim), "%llu MiB", (unsigned long long)(g_cfg.large_file_limit / (1024ull*1024ull)));

    ui_print_fit(15, 3, UI_INNER, C_WHITE, "Preset: %-9s   Full read: %-3s   Threshold: %s",
                 preset_name(g_cfg.preset), onoff(g_cfg.full_read), lim);
    ui_print_fit(16, 3, UI_INNER, C_WHITE, "Retries: %-2d      Consistency: %-3s  Chunk: %s",
                 g_cfg.read_retries, onoff(g_cfg.consistency_check), chunk_name(g_cfg.chunk_mode));
    ui_print_fit(17, 3, UI_INNER, C_WHITE, "Filters: skip folders=%-3s  skip exts=%-3s",
                 onoff(g_cfg.skip_known_folders), onoff(g_cfg.skip_media_exts));
    ui_print_fit(18, 3, UI_INNER, C_WHITE, "Quick: write test=%-3s  root listing=%-3s",
                 onoff(g_cfg.write_test), onoff(g_cfg.list_root));
    ui_print_fit(19, 3, UI_INNER, C_GRAY,  "Config: sdmc:/switch/sdcheck.cfg");

    ui_draw_box(1, 20, UI_W, 9, "Notes", C_CYAN);
    ui_print_fit(22, 3, UI_INNER, C_WHITE, "Deep Check is read-only and cannot repair the filesystem.");
    ui_print_fit(23, 3, UI_INNER, C_WHITE, "Quick write test (if enabled) writes a 4 KiB temp file and deletes it.");
    ui_print_fit(25, 3, UI_INNER, C_GRAY,  "Tip: Use Preset: Forensics for maximum coverage.");
    ui_print_fit(26, 3, UI_INNER, C_GRAY,  "Tip: If you use an overlay, set UI top margin to 1 or 2.");
}

static HomeAction ui_home(PadState* pad) {
    int sel = 0;
    while (appletMainLoop()) {
        log_set_context("Home");
        ui_home_draw(sel);
        consoleUpdate(NULL);

        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_ZL) { ui_help(pad); continue; }

        if (down & HidNpadButton_Up)   { if (sel > 0) sel--; }
        if (down & HidNpadButton_Down) { if (sel < 1) sel++; }

        if (down & HidNpadButton_A) return (sel == 0) ? HOME_ACT_QUICK : HOME_ACT_DEEP;
        if (down & HidNpadButton_X) return HOME_ACT_SETTINGS;
        if (down & HidNpadButton_Y) return HOME_ACT_LOG;

        if (down & HidNpadButton_Minus) {
            cfg_reset_defaults();
            log_push("INFO", "Defaults restored.");
            cfg_save_to_sd(&g_cfg, &g_ui);
            sel = 0;
        }

        if (down & HidNpadButton_Plus) return HOME_ACT_EXIT;
    }
    return HOME_ACT_EXIT;
}

static void ui_settings_draw(int sel, int scroll) {
    ui_draw_header("Settings",
                   "Up/Down: Select    Left/Right: Adjust\n"
                   "-: Reset defaults  B/+ : Back   ZL: Help\n"
                   "Y: Log");

    ui_draw_box(1, UI_CONTENT_Y, UI_W, 16, "Options (saved)", C_CYAN);

    /* Settings list */
    const int visible = 10;
    const int total = 14;
    if (scroll < 0) scroll = 0;
    if (scroll > total - visible) scroll = total - visible;
    if (scroll < 0) scroll = 0;

    int base_row = UI_CONTENT_Y + 2;

    for (int i = 0; i < visible; i++) {
        int idx = scroll + i;
        char mark[2] = " ";
        if (idx == sel) mark[0] = '>';

        char line[128];
        line[0] = 0;

        switch (idx) {
            case 0: snprintf(line, sizeof(line), "%s Preset              : %s", mark, preset_name(g_cfg.preset)); break;
            case 1: snprintf(line, sizeof(line), "%s Full read           : %s", mark, onoff(g_cfg.full_read)); break;
            case 2: snprintf(line, sizeof(line), "%s Large threshold     : %llu MiB", mark, (unsigned long long)(g_cfg.large_file_limit/(1024ull*1024ull))); break;
            case 3: snprintf(line, sizeof(line), "%s Read retries        : %d", mark, g_cfg.read_retries); break;
            case 4: snprintf(line, sizeof(line), "%s Consistency check   : %s", mark, onoff(g_cfg.consistency_check)); break;
            case 5: snprintf(line, sizeof(line), "%s Chunk size          : %s", mark, chunk_name(g_cfg.chunk_mode)); break;
            case 6: snprintf(line, sizeof(line), "%s Skip known folders  : %s", mark, onoff(g_cfg.skip_known_folders)); break;
            case 7: snprintf(line, sizeof(line), "%s Skip media exts     : %s", mark, onoff(g_cfg.skip_media_exts)); break;
            case 8: snprintf(line, sizeof(line), "%s Quick write test    : %s", mark, onoff(g_cfg.write_test)); break;
            case 9: snprintf(line, sizeof(line), "%s Quick root listing  : %s", mark, onoff(g_cfg.list_root)); break;
            case 10: snprintf(line, sizeof(line), "%s Deep scan target    : %s", mark, target_name(g_cfg.deep_target)); break;
            case 11: {
                char cr[80];
                snprintf(cr, sizeof(cr), "%.65s", g_cfg.custom_root[0] ? g_cfg.custom_root : "sdmc:/");
                snprintf(line, sizeof(line), "%s Custom path (cfg)   : %s", mark, cr);
            } break;
            case 12: snprintf(line, sizeof(line), "%s UI top margin       : %d", mark, g_ui.top_margin); break;
            case 13: snprintf(line, sizeof(line), "%s UI compact mode     : %s", mark, onoff(g_ui.compact_mode)); break;
            default: snprintf(line, sizeof(line), "%s ", mark); break;
        }

        const char* col = (idx == sel) ? C_GREEN : C_WHITE;
        ui_print_fit(base_row + i, 3, UI_INNER, col, "%s", line);
    }

    ui_draw_box(1, 22, UI_W, 7, "Help", C_CYAN);
    ui_print_fit(24, 3, UI_INNER, C_WHITE, "Saved: sdmc:/switch/sdcheck.cfg");
    ui_print_fit(25, 3, UI_INNER, C_WHITE, "Preset: Fast (skips common large areas) / Forensics (max coverage).");
    ui_print_fit(26, 3, UI_INNER, C_WHITE, "Consistency check reads small regions twice. Quick write test uses a 4 KiB temp file.");
}

static void ui_settings(PadState* pad) {
    int sel = 0;
    int scroll = 0;

    const uint64_t thresholds[] = { 64ull*1024ull*1024ull, 256ull*1024ull*1024ull, 1024ull*1024ull*1024ull };
    const int th_n = (int)(sizeof(thresholds)/sizeof(thresholds[0]));

    while (appletMainLoop()) {
        ui_settings_draw(sel, scroll);
        consoleUpdate(NULL);

        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_ZL) { ui_help(pad); continue; }
        if (down & HidNpadButton_Y) { ui_log(pad); continue; }

        if (down & HidNpadButton_Minus) {
            cfg_reset_defaults();
            log_push("INFO", "Defaults restored.");
            cfg_save_to_sd(&g_cfg, &g_ui);
            sel = 0; scroll = 0;
            continue;
        }

        if (down & HidNpadButton_Up) { if (sel > 0) sel--; }
        if (down & HidNpadButton_Down) { if (sel < 13) sel++; }

        const int visible = 10;
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + visible) scroll = sel - visible + 1;

        bool left  = (down & HidNpadButton_Left)  != 0;
        bool right = (down & HidNpadButton_Right) != 0;
        bool a     = (down & HidNpadButton_A)     != 0;

        if (left || right || a) {
            switch (sel) {
                case 0: {
                    PresetMode p = g_cfg.preset;
                    if (left) p = (p == PRESET_CUSTOM) ? PRESET_FORENSICS : (PresetMode)(p - 1);
                    else p = (p == PRESET_FORENSICS) ? PRESET_CUSTOM : (PresetMode)(p + 1);
                    apply_preset(&g_cfg, p);
                    log_pushf("INFO", "Preset set: %s", preset_name(g_cfg.preset));
                } break;
                case 1:
                    cfg_touch_custom(&g_cfg);
                    g_cfg.full_read = !g_cfg.full_read;
                    log_pushf("INFO", "Full read: %s", onoff(g_cfg.full_read));
                    break;
                case 2: {
                    cfg_touch_custom(&g_cfg);
                    int cur = 0;
                    for (int i = 0; i < th_n; i++) if (g_cfg.large_file_limit == thresholds[i]) { cur = i; break; }
                    if (left) cur = (cur - 1 + th_n) % th_n;
                    else cur = (cur + 1) % th_n;
                    g_cfg.large_file_limit = thresholds[cur];
                    log_pushf("INFO", "Large-file threshold: %llu MiB", (unsigned long long)(g_cfg.large_file_limit/(1024ull*1024ull)));
                } break;
                case 3:
                    cfg_touch_custom(&g_cfg);
                    if (left) g_cfg.read_retries = (g_cfg.read_retries > 0) ? (g_cfg.read_retries - 1) : 3;
                    else g_cfg.read_retries = (g_cfg.read_retries < 3) ? (g_cfg.read_retries + 1) : 0;
                    log_pushf("INFO", "Read retries: %d", g_cfg.read_retries);
                    break;
                case 4:
                    cfg_touch_custom(&g_cfg);
                    g_cfg.consistency_check = !g_cfg.consistency_check;
                    log_pushf("INFO", "Consistency check: %s", onoff(g_cfg.consistency_check));
                    break;
                case 5:
                    cfg_touch_custom(&g_cfg);
                    if (left) g_cfg.chunk_mode = (g_cfg.chunk_mode == CHUNK_AUTO) ? CHUNK_1M : (ChunkMode)(g_cfg.chunk_mode - 1);
                    else g_cfg.chunk_mode = (g_cfg.chunk_mode == CHUNK_1M) ? CHUNK_AUTO : (ChunkMode)(g_cfg.chunk_mode + 1);
                    log_pushf("INFO", "Chunk size: %s", chunk_name(g_cfg.chunk_mode));
                    break;
                case 6:
                    cfg_touch_custom(&g_cfg);
                    g_cfg.skip_known_folders = !g_cfg.skip_known_folders;
                    log_pushf("INFO", "Skip known folders: %s", onoff(g_cfg.skip_known_folders));
                    break;
                case 7:
                    cfg_touch_custom(&g_cfg);
                    g_cfg.skip_media_exts = !g_cfg.skip_media_exts;
                    log_pushf("INFO", "Skip media extensions: %s", onoff(g_cfg.skip_media_exts));
                    break;
                case 8:
                    g_cfg.write_test = !g_cfg.write_test;
                    log_pushf("INFO", "Quick write test: %s", onoff(g_cfg.write_test));
                    break;
                case 9:
                    g_cfg.list_root = !g_cfg.list_root;
                    log_pushf("INFO", "Quick root listing: %s", onoff(g_cfg.list_root));
                    break;
                case 10: {
                    int t = (int)g_cfg.deep_target;
                    if (left) t = (t == 0) ? (int)SCAN_TARGET_CUSTOM_CFG : (t - 1);
                    else t = (t == (int)SCAN_TARGET_CUSTOM_CFG) ? 0 : (t + 1);
                    g_cfg.deep_target = (ScanTarget)t;
                    log_pushf("INFO", "Deep scan target: %s", target_name(g_cfg.deep_target));
                } break;
                case 11:
                    log_push("INFO", "Custom path is read-only in UI. Edit sdmc:/switch/sdcheck.cfg (custom_root=...).");
                    break;
                case 12:
                    if (left) g_ui.top_margin = (g_ui.top_margin > 0) ? (g_ui.top_margin - 1) : 2;
                    else g_ui.top_margin = (g_ui.top_margin < 2) ? (g_ui.top_margin + 1) : 0;
                    log_pushf("INFO", "UI top margin: %d", g_ui.top_margin);
                    break;
                case 13:
                    g_ui.compact_mode = !g_ui.compact_mode;
                    log_pushf("INFO", "UI compact mode: %s", onoff(g_ui.compact_mode));
                    break;
                default:
                    break;
            }

            /* persist */
            cfg_save_to_sd(&g_cfg, &g_ui);
        }

        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return;
    }
}

/* --------------------------------------------------------------------------
   Quick Check (with double-confirm cancel)
----------------------------------------------------------------------------*/
static void ui_quick_running_frame(void) {
    ui_draw_header("Quick Check",
                   "Hold B/+/-: Cancel\n"
                   "Y: Log   ZL: Help\n"
                   " ");

    ui_draw_box(1, UI_CONTENT_Y, UI_W, 8,  "Progress", C_CYAN);
    ui_draw_box(1, 14, UI_W, 15, "Output",   C_CYAN);
}

static void ui_quick_set_line(int row, const char* color, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ui_print_fit(row, 3, UI_INNER, color ? color : C_WHITE, "%s", buf);
}

static bool ui_quick_handle_cancel(PadState* pad, uint64_t* hold_start_ms) {
    padUpdate(pad);
    uint64_t down = padGetButtonsDown(pad);
    uint64_t held = padGetButtons(pad);

    if (down & HidNpadButton_Y) {
        ui_log(pad);
        ui_quick_running_frame();
        return false;
    }

    if (down & HidNpadButton_ZL) {
        ui_help(pad);
        ui_quick_running_frame();
        return false;
    }

    const uint64_t cancel_mask = HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus;
    uint64_t now = now_ms();

    if (held & cancel_mask) {
        if (*hold_start_ms == 0) *hold_start_ms = now;
        else if ((now - *hold_start_ms) >= 650) {
            bool ok = ui_confirm_cancel(pad, "Quick Check");
            ui_wait_release(pad, cancel_mask, 1500);
            *hold_start_ms = 0;
            ui_quick_running_frame();
            return ok;
        }
    } else {
        *hold_start_ms = 0;
    }

    return false;
}


static bool ui_quick_plan(PadState* pad) {
redraw:
    log_set_context("Quick Check (plan)");
    ui_draw_header("Quick Check",
                   "A: Start          X: Settings\n"
                   "B/+ : Back        Y: Log   ZL: Help\n"
                   " ");

    ui_draw_box(1, UI_CONTENT_Y, UI_W, 9, "Scan plan", C_CYAN);
    ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, C_WHITE, "Step 1: Check sdmc:/ access");
    ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Step 2: Read space info (if available)");
    ui_print_fit(UI_CONTENT_Y + 4, 3, UI_INNER, C_WHITE, "Step 3: Optional root listing (first 12 entries)");
    ui_print_fit(UI_CONTENT_Y + 5, 3, UI_INNER, C_WHITE, "Step 4: Optional 4 KiB write test (temp file + delete)");

    ui_draw_box(1, UI_CONTENT_Y + 9, UI_W, 10, "Current settings", C_CYAN);
    ui_print_fit(UI_CONTENT_Y + 11, 3, UI_INNER, C_WHITE, "Quick write test: %s", onoff(g_cfg.write_test));
    ui_print_fit(UI_CONTENT_Y + 12, 3, UI_INNER, C_WHITE, "Quick root listing: %s", onoff(g_cfg.list_root));
    ui_print_fit(UI_CONTENT_Y + 13, 3, UI_INNER, C_WHITE, "Preset: %s   Filters: folders=%s exts=%s",
                 preset_name(g_cfg.preset), onoff(g_cfg.skip_known_folders), onoff(g_cfg.skip_media_exts));
    ui_print_fit(UI_CONTENT_Y + 14, 3, UI_INNER, C_GRAY,  "Saved to: sdmc:/switch/sdcheck.cfg");

    ui_draw_box(1, 22, UI_W, 7, "Buttons", C_CYAN);
    ui_print_fit(24, 3, UI_INNER, C_WHITE, "A: Start   B/+ : Back   X: Settings");
    ui_print_fit(25, 3, UI_INNER, C_WHITE, "Y: Log     ZL: Help");

    while (appletMainLoop()) {
        consoleUpdate(NULL);
        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_Y) { ui_log(pad); goto redraw; }
        if (down & HidNpadButton_ZL) { ui_help(pad); goto redraw; }
        if (down & HidNpadButton_X) { ui_settings(pad); cfg_save_to_sd(&g_cfg, &g_ui); goto redraw; }
        if (down & HidNpadButton_A) return true;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return false;
    }
    return false;
}

static void do_quick_check(PadState* pad) {
    if (!ui_quick_plan(pad)) return;

    RunResult rr;
    runresult_clear(&rr);
    rr.ran = true;
    rr.write_test_enabled = g_cfg.write_test;
    rr.effective_cfg = g_cfg;

    log_set_context("Quick Check (running)");
    log_push("INFO", "Quick Check started.");

    uint64_t start_tick = armGetSystemTick();
    uint64_t hold_ms = 0;

    ui_quick_running_frame();
    ui_quick_set_line(12, C_WHITE, "Step 1/4: SD access...");
    consoleUpdate(NULL);

    if (ui_quick_handle_cancel(pad, &hold_ms)) { rr.cancelled = true; goto quick_done; }

    if (access("sdmc:/", F_OK) != 0) {
        rr.sd_accessible = false;
        rr.open_errors++;
        log_pushf("ERROR", "sdmc:/ is not accessible (errno=%d).", errno);
        ui_quick_set_line(12, C_RED, "Step 1/4: SD access... FAILED");
        ui_quick_set_line(15, C_RED, "sdmc:/ is not accessible. Is the SD card inserted?");
        consoleUpdate(NULL);
        ui_message_screen(pad, "Quick Check", "sdmc:/ is not accessible. Is the SD card inserted?", "B/+ : Back\nY: Log   ZL: Help\n ");
        goto quick_done;
    }

    rr.sd_accessible = true;
    ui_quick_set_line(12, C_GREEN, "Step 1/4: SD access... OK");
    consoleUpdate(NULL);

    if (ui_quick_handle_cancel(pad, &hold_ms)) { rr.cancelled = true; goto quick_done; }

    ui_quick_set_line(11, C_WHITE, "Step 2/4: Space info...");
    consoleUpdate(NULL);

    bool space_ok = get_sd_space(&rr.space);
    if (!space_ok) {
        rr.space_ok = false;
        rr.stat_errors++;
        ui_quick_set_line(11, C_YELLOW, "Step 2/4: Space info... WARN (unavailable)");
    } else {
        rr.space_ok = true;
        char t[32], u[32], f[32];
        format_bytes(t, sizeof(t), rr.space.total);
        format_bytes(u, sizeof(u), rr.space.used);
        format_bytes(f, sizeof(f), rr.space.free);
        ui_quick_set_line(11, C_GREEN, "Step 2/4: Space info... OK");
        ui_quick_set_line(16, C_WHITE, "SD space: total %s   used %s   free %s", t, u, f);
    }
    consoleUpdate(NULL);

    if (ui_quick_handle_cancel(pad, &hold_ms)) { rr.cancelled = true; goto quick_done; }

    ui_quick_set_line(12, C_WHITE, "Step 3/4: Root listing...");
    consoleUpdate(NULL);

    if (!g_cfg.list_root) {
        rr.root_ok = true;
        ui_quick_set_line(12, C_GRAY, "Step 3/4: Root listing... OFF");
        ui_quick_set_line(17, C_GRAY, "Root listing: OFF");
    } else {
        DIR* d = opendir("sdmc:/");
        if (!d) {
            rr.open_errors++;
            rr.root_ok = false;
            log_pushf("ERROR", "Root listing: opendir failed: %s", strerror(errno));
            ui_quick_set_line(12, C_RED, "Step 3/4: Root listing... FAILED");
            ui_quick_set_line(17, C_RED, "Root listing failed: %s", strerror(errno));
        } else {
            rr.root_ok = true;
            ui_quick_set_line(12, C_GREEN, "Step 3/4: Root listing... OK");
            ui_quick_set_line(17, C_WHITE, "Root entries (first 12):");
            struct dirent* e;
            int shown = 0;
            int row = 18;
            while ((e = readdir(d)) != NULL && shown < 12) {
                if (ui_quick_handle_cancel(pad, &hold_ms)) { rr.cancelled = true; break; }
                ui_print_fit(row++, 3, UI_INNER, C_WHITE, "- %.72s", e->d_name);
                shown++;
            }
            closedir(d);
        }
    }
    consoleUpdate(NULL);
    if (rr.cancelled) goto quick_done;

    if (ui_quick_handle_cancel(pad, &hold_ms)) { rr.cancelled = true; goto quick_done; }

    ui_quick_set_line(11, C_WHITE, "Step 4/4: Write test...");
    consoleUpdate(NULL);

    if (!g_cfg.write_test) {
        rr.write_test_ok = false;
        ui_quick_set_line(11, C_GRAY, "Step 4/4: Write test... OFF");
        ui_quick_set_line(24, C_YELLOW, "Write test: OFF (read-only quick run)");
    } else {
        rr.write_test_ok = quick_rw_test();
        if (rr.write_test_ok) {
            rr.bytes_read += 4096;
            ui_quick_set_line(11, C_GREEN, "Step 4/4: Write test... OK");
            ui_quick_set_line(24, C_GREEN, "Write test: OK");
        } else {
            rr.read_errors++;
            ui_quick_set_line(11, C_RED, "Step 4/4: Write test... FAILED");
            ui_quick_set_line(24, C_RED, "Write test: FAILED");
        }
    }
    consoleUpdate(NULL);

quick_done:
    {
        uint64_t end_tick = armGetSystemTick();
        rr.seconds = ticks_to_seconds(end_tick - start_tick);
        rr.dirs_total = 0;
        rr.files_total = 0;
        rr.files_read = 0;
        rr.skipped_dirs = 0;
        rr.skipped_files = 0;

        rr.verdict = compute_verdict(&rr);

        log_set_context("Quick Check (results)");
        rr.log_saved = (access("sdmc:/", F_OK) == 0);
        rr.log_save_ok = rr.log_saved ? log_save_to_sdroot(&g_cfg) : false;

        ui_results(pad, "Quick Check - Results", &rr);
        log_set_context("Home");
    }
}

/* --------------------------------------------------------------------------
   Deep Check
----------------------------------------------------------------------------*/
static void do_deep_check(PadState* pad) {
    if (access("sdmc:/", F_OK) != 0) {
        log_pushf("ERROR", "sdmc:/ is not accessible (errno=%d).", errno);
        ui_message_screen(pad, "Deep Check", "sdmc:/ is not accessible. Is the SD card inserted?", "B/+ : Back\nY: Log   ZL: Help\n ");
        return;
    }

    const uint64_t thresholds[] = { 64ull*1024ull*1024ull, 256ull*1024ull*1024ull, 1024ull*1024ull*1024ull };
    const int th_n = (int)(sizeof(thresholds)/sizeof(thresholds[0]));

    while (appletMainLoop()) {
        ui_draw_header("Deep Check",
                       "A: Start           ZR: Toggle Full read\n"
                       "Left/Right: Threshold   B/+ : Back\n"
                       "Y: Log   ZL: Help");

        ui_draw_box(1, UI_CONTENT_Y, UI_W, 11, "Policy", C_CYAN);
        ui_print_fit(UI_CONTENT_Y + 2, 3, UI_INNER, C_WHITE, "Preset: %s", preset_name(g_cfg.preset));
        char plan_root[256];
        get_deep_root(&g_cfg, plan_root, sizeof(plan_root));
        ui_print_fit(UI_CONTENT_Y + 3, 3, UI_INNER, C_WHITE, "Target: %s", target_name(g_cfg.deep_target));
        ui_print_fit(UI_CONTENT_Y + 4, 3, UI_INNER, C_WHITE, "Scan root: %.60s", plan_root);
        if (g_cfg.deep_target == SCAN_TARGET_CUSTOM_CFG) {
            ui_print_fit(UI_CONTENT_Y + 5, 3, UI_INNER, C_GRAY,  "Custom path (cfg): %.60s", g_cfg.custom_root);
        }
        ui_print_fit(UI_CONTENT_Y + 6, 3, UI_INNER, C_WHITE, "Large-file threshold: %llu MiB", (unsigned long long)(g_cfg.large_file_limit/(1024ull*1024ull)));
        ui_print_fit(UI_CONTENT_Y + 7, 3, UI_INNER, C_WHITE, "Full read: %s", onoff(g_cfg.full_read));
        ui_print_fit(UI_CONTENT_Y + 8, 3, UI_INNER, C_GRAY,  "If Full read is OFF, large files may be sampled (first+last 64 KiB)." );

        ui_draw_box(1, 17, UI_W, 12, "Notes", C_CYAN);
        ui_print_fit(19, 3, UI_INNER, C_WHITE, "Deep Check reads files to detect read errors.");
        ui_print_fit(20, 3, UI_INNER, C_WHITE, "It does NOT repair the filesystem.");
        ui_print_fit(22, 3, UI_INNER, C_GRAY,  "During scan: X pauses, Y shows log (pause), hold B/+/- cancels with confirm.");
        ui_print_fit(23, 3, UI_INNER, C_GRAY,  "Filters/presets are configured in Settings.");

        consoleUpdate(NULL);

        uint64_t down = poll_down(pad);
        if (down & HidNpadButton_Y) { ui_log(pad); continue; }
        if (down & HidNpadButton_ZL) { ui_help(pad); continue; }
        if (down & HidNpadButton_X) { ui_settings(pad); cfg_save_to_sd(&g_cfg, &g_ui); continue; }

        if (down & HidNpadButton_ZR) {
            cfg_touch_custom(&g_cfg);
            g_cfg.full_read = !g_cfg.full_read;
            log_pushf("INFO", "Full read toggled: %s", onoff(g_cfg.full_read));
            cfg_save_to_sd(&g_cfg, &g_ui);
        }

        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
            cfg_touch_custom(&g_cfg);
            int cur = 0;
            for (int i = 0; i < th_n; i++) if (g_cfg.large_file_limit == thresholds[i]) { cur = i; break; }
            if (down & HidNpadButton_Left) cur = (cur - 1 + th_n) % th_n;
            if (down & HidNpadButton_Right) cur = (cur + 1) % th_n;
            g_cfg.large_file_limit = thresholds[cur];
            log_pushf("INFO", "Large-file threshold set: %llu MiB", (unsigned long long)(g_cfg.large_file_limit/(1024ull*1024ull)));
            cfg_save_to_sd(&g_cfg, &g_ui);
        }

        if (down & HidNpadButton_A) break;
        if (down & (HidNpadButton_B | HidNpadButton_Plus)) return;
    }

    log_push("INFO", "Deep Check started.");

    ScanConfig cfg = g_cfg;

    char deep_root[256];
    get_deep_root(&cfg, deep_root, sizeof(deep_root));
    struct stat root_st;
    if (stat(deep_root, &root_st) != 0 || !S_ISDIR(root_st.st_mode)) {
        log_pushf("ERROR", "Target root is not accessible: %s (%s)", deep_root, strerror(errno));
        ui_message_screen(pad, "Deep Check", "Target root is not accessible.", "B/+ : Back\nY: Log   ZL: Help\n ");
        return;
    }

    log_set_context("Deep Check (running)");

    ScanStats st;
    memset(&st, 0, sizeof(st));
    st.ui_active = true;
    st.ui_drawn = false;
    st.ui_start_ms = now_ms();
    st.ui_last_ms = 0;
    st.input_last_ms = 0;
    st.cancelled = false;
    st.paused = false;
    st.paused_total_ms = 0;

    st.run_full_read = cfg.full_read;
    st.run_large_limit = cfg.large_file_limit;
    st.run_retries = cfg.read_retries;
    st.run_consistency = cfg.consistency_check;
    st.run_skip_folders = cfg.skip_known_folders;
    st.run_skip_exts = cfg.skip_media_exts;
    st.run_chunk = cfg.chunk_mode;

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(st.wall_start_str, sizeof(st.wall_start_str), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    uint64_t start_tick = armGetSystemTick();

    scan_engine_run(deep_root, &cfg, &st, pad, deep_ui_maybe_update);

    st.ui_active = false;

    uint64_t end_tick = armGetSystemTick();
    double secs = ticks_to_seconds(end_tick - start_tick);

    RunResult rr;
    runresult_clear(&rr);
    rr.ran = true;
    rr.cancelled = st.cancelled;

    rr.dirs_total = st.dirs_total;
    rr.files_total = st.files_total;
    rr.files_read = st.files_read;
    rr.bytes_read = st.bytes_read;
    rr.seconds = secs;

    rr.open_errors = st.open_errors;
    rr.read_errors = st.read_errors;
    rr.read_errors_transient = st.read_errors_transient;
    rr.stat_errors = st.stat_errors;
    rr.path_errors = st.path_errors;
    rr.consistency_errors = st.consistency_errors;

    rr.skipped_dirs = st.skipped_dirs;
    rr.skipped_files = st.skipped_files;

    rr.effective_cfg = cfg;

    rr.largest_count = st.largest_count;
    for (int i = 0; i < st.largest_count && i < LARGEST_MAX; i++) rr.largest[i] = st.largest[i];

    rr.fail_count = st.fail_count;
    for (int i = 0; i < st.fail_count && i < FAIL_MAX; i++) snprintf(rr.fail_paths[i], sizeof(rr.fail_paths[i]), "%s", st.fail_paths[i]);

    rr.perf_ops = st.perf_ops;
    rr.perf_bytes = st.perf_bytes;
    for (int i = 0; i < 5; i++) rr.perf_hist[i] = st.perf_hist[i];
    rr.perf_stalls = st.perf_stalls;
    rr.perf_stall_total_ms = st.perf_stall_total_ms;
    rr.perf_longest_ms = st.perf_longest_ms;
    rr.perf_longest_mib_s = st.perf_longest_mib_s;
    rr.perf_longest_off = st.perf_longest_off;
    rr.perf_longest_bytes = st.perf_longest_bytes;
    snprintf(rr.perf_longest_path, sizeof(rr.perf_longest_path), "%s", st.perf_longest_path);

    rr.first_fail_set = st.first_fail_set;
    snprintf(rr.first_fail_kind, sizeof(rr.first_fail_kind), "%s", st.first_fail_kind);
    snprintf(rr.first_fail_path, sizeof(rr.first_fail_path), "%s", st.first_fail_path);
    rr.first_fail_off = st.first_fail_off;
    rr.first_fail_bytes = st.first_fail_bytes;
    rr.first_fail_errno = st.first_fail_errno;
    snprintf(rr.first_fail_note, sizeof(rr.first_fail_note), "%s", st.first_fail_note);

    rr.verdict = compute_verdict(&rr);

    log_set_context("Deep Check (results)");
    rr.log_saved = (access("sdmc:/", F_OK) == 0);
    rr.log_save_ok = rr.log_saved ? log_save_to_sdroot(&cfg) : false;

    ui_results(pad, "Deep Check - Results", &rr);
    log_set_context("Home");
}

/* --------------------------------------------------------------------------
   Entry
----------------------------------------------------------------------------*/
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    cfg_reset_defaults();

    consoleInit(NULL);
    ui_hide_cursor();
    ui_clear_screen();

    log_clear();
    log_push("INFO", "SD Check started.");

    sleep_guard_enter(&g_sleep);

    const uint64_t style =
        (uint64_t)HidNpadStyleSet_NpadFullKey  |
        (uint64_t)HidNpadStyleSet_NpadHandheld |
        (uint64_t)HidNpadStyleSet_NpadJoyDual  |
        (uint64_t)HidNpadStyleSet_NpadJoyLeft  |
        (uint64_t)HidNpadStyleSet_NpadJoyRight;

    padConfigureInput(1, style);

    PadState pad;
    padInitializeDefault(&pad);

    Result rc = fsInitialize();
    if (R_FAILED(rc)) {
        log_pushf("ERROR", "fsInitialize failed: 0x%X", rc);
        ui_message_screen(&pad, "Fatal error", "fsInitialize failed. Cannot continue.", "B/+ : Exit\n \n ");
        sleep_guard_leave(&g_sleep);
        ui_show_cursor();
        consoleExit(NULL);
        return 0;
    }

    bool sd_mounted = false;
    rc = fsdevMountSdmc();
    if (R_SUCCEEDED(rc)) {
        sd_mounted = true;
        log_push("INFO", "SD mounted via fsdevMountSdmc().");
    } else {
        if (access("sdmc:/", F_OK) == 0) {
            log_pushf("INFO", "fsdevMountSdmc failed (0x%X) but sdmc:/ is accessible (already mounted).", rc);
        } else {
            log_pushf("WARN", "fsdevMountSdmc failed: 0x%X", rc);
        }
    }

    /* Load persisted settings (optional) */
    if (access("sdmc:/", F_OK) == 0) {
        if (cfg_load_from_sd(&g_cfg, &g_ui)) {
            log_push("INFO", "Loaded settings: sdmc:/switch/sdcheck.cfg");
        } else {
            log_push("INFO", "No settings file (sdmc:/switch/sdcheck.cfg). Using defaults.");
        }
    }


    while (appletMainLoop()) {
        HomeAction act = ui_home(&pad);

        if (act == HOME_ACT_QUICK) do_quick_check(&pad);
        else if (act == HOME_ACT_DEEP) do_deep_check(&pad);
        else if (act == HOME_ACT_SETTINGS) ui_settings(&pad);
        else if (act == HOME_ACT_LOG) ui_log(&pad);
        else if (act == HOME_ACT_EXIT) break;
    }

    sleep_guard_leave(&g_sleep);
    ui_show_cursor();

    if (sd_mounted) fsdevUnmountAll();
    fsExit();
    consoleExit(NULL);
    return 0;
}
