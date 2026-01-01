#pragma once
#include "app.h"

/* String helpers */
void trim_ws(char* s);
int  parse_bool(const char* v, int defv);

/* Formatting */
void format_bytes(char* out, size_t out_sz, uint64_t b);
void format_hms(char* out, size_t out_sz, uint64_t ms);
void tail_ellipsize(char* out, size_t out_sz, const char* in, size_t keep);
const char* onoff(bool v);


/* Time */
static inline uint64_t now_ms(void) {
    return armTicksToNs(armGetSystemTick()) / 1000000ULL;
}

double ticks_to_seconds(uint64_t ticks);
