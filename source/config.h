#pragma once
#include "app.h"

typedef enum {
    PRESET_CUSTOM = 0,
    PRESET_FAST = 1,
    PRESET_FORENSICS = 2
} PresetMode;

typedef enum {
    CHUNK_AUTO = 0,
    CHUNK_128K,
    CHUNK_256K,
    CHUNK_512K,
    CHUNK_1M
} ChunkMode;

typedef enum {
    SCAN_TARGET_ALL = 0,      /* sdmc:/ */
    SCAN_TARGET_NINTENDO,     /* sdmc:/Nintendo */
    SCAN_TARGET_EMUMMC,       /* sdmc:/emuMMC */
    SCAN_TARGET_SWITCH,       /* sdmc:/switch */
    SCAN_TARGET_CUSTOM_CFG    /* custom_root from cfg */
} ScanTarget;

const char* preset_name(PresetMode p);
const char* chunk_name(ChunkMode m);
const char* target_name(ScanTarget t);

typedef struct {
    PresetMode preset;

    bool     full_read;
    uint64_t large_file_limit;  /* bytes */

    int      read_retries;      /* 0..3 */
    bool     consistency_check; /* read same region twice and compare CRC */

    ChunkMode chunk_mode;

    bool     skip_known_folders;
    bool     skip_media_exts;

    /* Deep-check target root */
    ScanTarget deep_target;
    char     custom_root[256]; /* used when deep_target == SCAN_TARGET_CUSTOM_CFG */

    /* Quick-check options */
    bool     write_test;  /* creates/deletes sdmc:/_sdcheck_tmp.bin */
    bool     list_root;
} ScanConfig;

typedef struct {
    int  top_margin;    /* 0..2 */
    bool compact_mode;
} UiConfig;

extern const ScanConfig g_cfg_defaults;
extern ScanConfig g_cfg;
extern const UiConfig g_ui_defaults;
extern UiConfig g_ui;

void apply_preset(ScanConfig* cfg, PresetMode mode);
void cfg_touch_custom(ScanConfig* cfg);
void cfg_reset_defaults(void);

/* Persistent config (sdmc:/switch/sdcheck.cfg) */
bool cfg_save_to_sd(const ScanConfig* cfg, const UiConfig* ui);
bool cfg_load_from_sd(ScanConfig* cfg, UiConfig* ui);
const char* cfg_file_path(void);
