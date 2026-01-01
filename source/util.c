#include "util.h"

void trim_ws(char* s) {
    if (!s) return;
    /* ltrim */
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') i++;
    if (i) memmove(s, s + i, strlen(s + i) + 1);
    /* rtrim */
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) { s[n-1] = 0; n--; }
}

int parse_bool(const char* v, int defv) {
    if (!v) return defv;
    if (v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T') return 1;
    if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F') return 0;
    return defv;
}

void format_bytes(char* out, size_t out_sz, uint64_t b) {
    const char* u[] = {"B","KiB","MiB","GiB","TiB"};
    double v = (double)b;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    snprintf(out, out_sz, "%.2f %s", v, u[i]);
}


void format_hms(char* out, size_t out_sz, uint64_t ms) {
    if (!out || out_sz < 9) { if (out && out_sz) out[0] = 0; return; }
    uint64_t s = ms / 1000ULL;
    uint64_t h = s / 3600ULL; s %= 3600ULL;
    uint64_t m = s / 60ULL;   s %= 60ULL;
    if (h > 99ULL) snprintf(out, out_sz, "99+:%02u:%02u", (unsigned)m, (unsigned)s);
    else snprintf(out, out_sz, "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
}

void tail_ellipsize(char* out, size_t out_sz, const char* in, size_t keep) {
    if (!out || out_sz == 0) return;
    if (!in) { out[0] = 0; return; }
    size_t len = strlen(in);
    if (len <= keep || keep < 8) { snprintf(out, out_sz, "%s", in); return; }
    size_t tail = keep - 3;
    const char* p = in + (len - tail);
    snprintf(out, out_sz, "...%s", p);
}

const char* onoff(bool v) { return v ? "ON" : "OFF"; }

double ticks_to_seconds(uint64_t ticks) {
    static uint64_t freq = 0;
    if (!freq) freq = armGetSystemTickFreq();
    if (!freq) return 0.0;
    return (double)ticks / (double)freq;
}
