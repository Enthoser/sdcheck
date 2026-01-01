#pragma once
#include "app.h"
#include "config.h"

typedef struct {
    uint64_t dirs_total;
    uint64_t files_total;
    uint64_t files_read;
    uint64_t bytes_read;

    uint64_t open_errors;
    uint64_t read_errors;          /* persistent */
    uint64_t read_errors_transient;/* recovered by retry */
    uint64_t stat_errors;
    uint64_t path_errors;
    uint64_t consistency_errors;

    uint64_t skipped_dirs;
    uint64_t skipped_files;

    bool cancelled;

    /* UI */
    bool ui_active;
    bool ui_drawn;
    uint64_t ui_start_ms;
    uint64_t ui_last_ms;
    uint64_t input_last_ms;

    bool paused;
    uint64_t pause_start_ms;
    uint64_t paused_total_ms;

    uint64_t cancel_hold_start_ms;
    bool     cancel_prompt_active;

    uint64_t speed_last_ms;
    uint64_t speed_last_bytes;
    double   speed_mib_s;

    time_t   wall_start;
    char     wall_start_str[16];

    char     current_path[256];
    uint64_t current_size;
    uint64_t current_planned;
    uint64_t current_done;
    bool     current_sample;

    char err_ring[ERR_RING_MAX][256];
    int  err_ring_count;

    /* Largest files */
    LargestEntry largest[LARGEST_MAX];
    int largest_count;

    /* First failing paths (unique, truncated) */
    char fail_paths[FAIL_MAX][256];
    int fail_count;

    /* Performance tracking (MiB/s histogram, stalls, longest op) */
    uint64_t perf_ops;
    uint64_t perf_bytes;
    uint64_t perf_hist[5]; /* >=60, >=30, >=10, >=1, <1 MiB/s */
    uint64_t perf_stalls;
    uint64_t perf_stall_total_ms;
    uint64_t perf_longest_ms;
    double   perf_longest_mib_s;
    uint64_t perf_longest_off;
    uint64_t perf_longest_bytes;
    char     perf_longest_path[256];

    /* First failure context (first non-OK condition) */
    bool     first_fail_set;
    char     first_fail_kind[16];
    char     first_fail_path[256];
    uint64_t first_fail_off;
    uint64_t first_fail_bytes;
    int      first_fail_errno;
    char     first_fail_note[96];

    /* Effective run config subset for UI */
    bool run_full_read;
    uint64_t run_large_limit;
    int run_retries;
    bool run_consistency;
    bool run_skip_folders;
    bool run_skip_exts;
    ChunkMode run_chunk;
} ScanStats;

typedef void (*ScanUiUpdateFn)(ScanStats* st, PadState* pad, bool force);

/* Elapsed wall time excluding pauses (milliseconds). */
uint64_t scan_stats_elapsed_ms(const ScanStats* st, uint64_t now_ms);

/*
 * Performs the deep traversal+read. UI/input handling remains in the caller via ui_update.
 * Returns true if traversal completed (even with errors). Returns false only on fatal setup failure.
 */
bool scan_engine_run(const char* root, const ScanConfig* cfg, ScanStats* st, PadState* pad, ScanUiUpdateFn ui_update);
