#include "log.h"

typedef struct {
    char lines[LOG_RING_MAX][256];
    int  count;
} LogRing;

static LogRing g_log;
static LogSaveStatus g_log_save = {0};
static char g_log_context[64] = "Menu";
static const char LOG_FILE_PATH[] = "sdmc:/sdcheck.log";

void log_clear(void) {
    memset(&g_log, 0, sizeof(g_log));
}

void log_save_status_set(bool ok, const char* note) {
    g_log_save.known = true;
    g_log_save.ok = ok;
    g_log_save.when = time(NULL);

    struct tm tmv;
    localtime_r(&g_log_save.when, &tmv);
    snprintf(g_log_save.when_str, sizeof(g_log_save.when_str), "%02d:%02d:%02d",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

    if (note && note[0]) snprintf(g_log_save.note, sizeof(g_log_save.note), "%s", note);
    else g_log_save.note[0] = 0;
}

const LogSaveStatus* log_save_status(void) {
    return &g_log_save;
}

void log_set_context(const char* ctx) {
    if (ctx && ctx[0]) snprintf(g_log_context, sizeof(g_log_context), "%s", ctx);
    else snprintf(g_log_context, sizeof(g_log_context), "Menu");
}

const char* log_get_context(void) {
    return g_log_context;
}

const char* log_file_path(void) {
    return LOG_FILE_PATH;
}

void log_push(const char* level, const char* msg) {
    if (!level || !msg) return;
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);

    char line[256];
    snprintf(line, sizeof(line), "[%02d:%02d:%02d] %s: %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, level, msg);

    int idx = g_log.count % LOG_RING_MAX;
    snprintf(g_log.lines[idx], sizeof(g_log.lines[idx]), "%s", line);
    g_log.count++;
}

void log_pushf(const char* level, const char* fmt, ...) {
    if (!fmt) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_push(level ? level : "INFO", buf);
}

int log_ring_count(void) {
    int total = g_log.count;
    if (total < 0) total = 0;
    return (total > LOG_RING_MAX) ? LOG_RING_MAX : total;
}

const char* log_ring_line(int oldest_index) {
    int available = log_ring_count();
    if (available <= 0) return NULL;
    if (oldest_index < 0) oldest_index = 0;
    if (oldest_index >= available) oldest_index = available - 1;

    int total = g_log.count;
    if (total < 0) total = 0;
    int start = total - available;
    int start_idx = start % LOG_RING_MAX;
    int idx = (start_idx + oldest_index) % LOG_RING_MAX;
    return g_log.lines[idx];
}
