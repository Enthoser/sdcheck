#pragma once
#include "app.h"

typedef struct {
    bool   known;
    bool   ok;
    time_t when;
    char   when_str[16];
    char   note[128];
} LogSaveStatus;

void log_clear(void);
void log_push(const char* level, const char* msg);
void log_pushf(const char* level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

/* Ring access (oldest-first) */
int  log_ring_count(void);
const char* log_ring_line(int oldest_index);

/* Save status */
void log_save_status_set(bool ok, const char* note);
const LogSaveStatus* log_save_status(void);

/* Context */
void log_set_context(const char* ctx);
const char* log_get_context(void);

/* File path */
const char* log_file_path(void);
