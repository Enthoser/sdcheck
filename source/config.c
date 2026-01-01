#include "config.h"
#include "util.h"
#include "log.h"

static const char CFG_DIR_PATH[]  = "sdmc:/switch";
static const char CFG_FILE_PATH[] = "sdmc:/switch/sdcheck.cfg";
static const char CFG_TMP_PATH[]  = "sdmc:/switch/sdcheck.cfg.tmp";

const char* cfg_file_path(void) {
    return CFG_FILE_PATH;
}

const char* preset_name(PresetMode p) {
    switch (p) {
        case PRESET_FAST: return "Fast";
        case PRESET_FORENSICS: return "Forensics";
        default: return "Custom";
    }
}

const char* chunk_name(ChunkMode m) {
    switch (m) {
        case CHUNK_128K: return "128 KiB";
        case CHUNK_256K: return "256 KiB";
        case CHUNK_512K: return "512 KiB";
        case CHUNK_1M:   return "1 MiB";
        default:         return "Auto";
    }
}

const char* target_name(ScanTarget t) {
    switch (t) {
        case SCAN_TARGET_NINTENDO:   return "Nintendo";
        case SCAN_TARGET_EMUMMC:     return "emuMMC";
        case SCAN_TARGET_SWITCH:     return "switch";
        case SCAN_TARGET_CUSTOM_CFG: return "Custom (cfg)";
        default:                     return "All";
    }
}

static bool sanitize_custom_root(char* io, size_t io_sz) {
    if (!io || io_sz == 0) return false;
    trim_ws(io);
    if (!io[0]) return false;

    const char* prefix = "sdmc:/";
    const size_t plen = strlen(prefix);
    const size_t inlen = strlen(io);
    if (inlen < plen) return false;

    /* case-insensitive prefix check */
    for (size_t i = 0; i < plen; i++) {
        char a = tolower((unsigned char)io[i]);
        char b = prefix[i];
        if (a != b) return false;
    }

    /* canonicalize prefix */
    memcpy(io, prefix, plen);

    /* drop trailing whitespace and trailing slashes (keep sdmc:/) */
    size_t n = strlen(io);
    while (n > 0 && (io[n-1] == ' ' || io[n-1] == '\t' || io[n-1] == '\r' || io[n-1] == '\n')) { io[n-1] = 0; n--; }
    while (n > plen && io[n-1] == '/') { io[n-1] = 0; n--; }

    /* validate segments: reject '.' or '..' path elements and backslashes */
    const char* s = io + plen;
    while (*s) {
        while (*s == '/') s++;
        if (!*s) break;

        const char* seg = s;
        while (*s && *s != '/') {
            if (*s == '\\') return false;
            s++;
        }
        size_t seglen = (size_t)(s - seg);
        if (seglen == 1 && seg[0] == '.') return false;
        if (seglen == 2 && seg[0] == '.' && seg[1] == '.') return false;
    }

    if (strlen(io) >= io_sz) io[io_sz-1] = 0;
    return true;
}


static void set_default_custom_root(char* out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    snprintf(out, out_sz, "sdmc:/");
}
const ScanConfig g_cfg_defaults = {
    .preset = PRESET_CUSTOM,
    .full_read = false,
    .large_file_limit = 256ull * 1024ull * 1024ull,
    .read_retries = 1,
    .consistency_check = false,
    .chunk_mode = CHUNK_AUTO,
    .skip_known_folders = false,
    .skip_media_exts = false,
    .deep_target = SCAN_TARGET_ALL,
    .custom_root = "sdmc:/",
    .write_test = false,
    .list_root = true,
};

ScanConfig g_cfg;

const UiConfig g_ui_defaults = {
    .top_margin = 1,
    .compact_mode = false
};

UiConfig g_ui;

void cfg_reset_defaults(void) {
    g_cfg = g_cfg_defaults;
    g_ui = g_ui_defaults;
}

void apply_preset(ScanConfig* cfg, PresetMode mode) {
    if (!cfg) return;
    cfg->preset = mode;

    if (mode == PRESET_FAST) {
        cfg->full_read = false;
        cfg->large_file_limit = 64ull * 1024ull * 1024ull;
        cfg->read_retries = 1;
        cfg->consistency_check = false;
        cfg->chunk_mode = CHUNK_AUTO;
        cfg->skip_known_folders = true;
        cfg->skip_media_exts = true;
    } else if (mode == PRESET_FORENSICS) {
        cfg->full_read = true;
        cfg->large_file_limit = 1024ull * 1024ull * 1024ull;
        cfg->read_retries = 2;
        cfg->consistency_check = true;
        cfg->chunk_mode = CHUNK_AUTO;
        cfg->skip_known_folders = false;
        cfg->skip_media_exts = false;
    }
}

void cfg_touch_custom(ScanConfig* cfg) {
    if (!cfg) return;
    if (cfg->preset != PRESET_CUSTOM) cfg->preset = PRESET_CUSTOM;
}

bool cfg_save_to_sd(const ScanConfig* cfg, const UiConfig* ui) {
    if (!cfg || !ui) return false;
    if (access("sdmc:/", F_OK) != 0) {
        log_push("WARN", "Config save skipped: sdmc:/ not accessible.");
        return false;
    }

    mkdir(CFG_DIR_PATH, 0777);

    FILE* f = fopen(CFG_TMP_PATH, "wb");
    if (!f) {
        log_pushf("WARN", "Config save failed (tmp open): %s", strerror(errno));
        return false;
    }

    fprintf(f, "preset=%d\n", (int)cfg->preset);
    fprintf(f, "full_read=%d\n", cfg->full_read ? 1 : 0);
    fprintf(f, "large_file_limit_mib=%llu\n", (unsigned long long)(cfg->large_file_limit / (1024ull*1024ull)));
    fprintf(f, "read_retries=%d\n", cfg->read_retries);
    fprintf(f, "consistency_check=%d\n", cfg->consistency_check ? 1 : 0);
    fprintf(f, "chunk_mode=%d\n", (int)cfg->chunk_mode);
    fprintf(f, "skip_known_folders=%d\n", cfg->skip_known_folders ? 1 : 0);
    fprintf(f, "skip_media_exts=%d\n", cfg->skip_media_exts ? 1 : 0);
    fprintf(f, "deep_target=%d\n", (int)cfg->deep_target);
    fprintf(f, "custom_root=%s\n", cfg->custom_root[0] ? cfg->custom_root : "sdmc:/");
    fprintf(f, "write_test=%d\n", cfg->write_test ? 1 : 0);
    fprintf(f, "list_root=%d\n", cfg->list_root ? 1 : 0);

    fprintf(f, "ui_top_margin=%d\n", ui->top_margin);
    fprintf(f, "ui_compact_mode=%d\n", ui->compact_mode ? 1 : 0);

    fflush(f);
    fclose(f);

    remove(CFG_FILE_PATH);
    if (rename(CFG_TMP_PATH, CFG_FILE_PATH) != 0) {
        FILE* src = fopen(CFG_TMP_PATH, "rb");
        FILE* dst = fopen(CFG_FILE_PATH, "wb");
        if (src && dst) {
            char buf[1024];
            size_t r;
            while ((r = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, r, dst);
        }
        if (src) fclose(src);
        if (dst) fclose(dst);
        remove(CFG_TMP_PATH);

        if (access(CFG_FILE_PATH, F_OK) == 0) {
            log_push("INFO", "Config saved: sdmc:/switch/sdcheck.cfg");
            return true;
        }

        log_pushf("WARN", "Config save failed (rename): %s", strerror(errno));
        return false;
    }

    log_push("INFO", "Config saved: sdmc:/switch/sdcheck.cfg");
    return true;
}

bool cfg_load_from_sd(ScanConfig* cfg, UiConfig* ui) {
    if (!cfg || !ui) return false;
    if (access(CFG_FILE_PATH, F_OK) != 0) return false;

    FILE* f = fopen(CFG_FILE_PATH, "rb");
    if (!f) return false;

    bool have_preset = false;
    int preset = (int)cfg->preset;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        trim_ws(line);
        if (!line[0]) continue;
        if (line[0] == '#') continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = line;
        char* val = eq + 1;
        trim_ws(key);
        trim_ws(val);

        if (!key[0]) continue;

        if (strcmp(key, "preset") == 0) { preset = atoi(val); have_preset = true; }
        else if (strcmp(key, "full_read") == 0) cfg->full_read = parse_bool(val, cfg->full_read) != 0;
        else if (strcmp(key, "large_file_limit_mib") == 0) {
            unsigned long long mib = strtoull(val, NULL, 10);
            if (mib < 16) mib = 16;
            if (mib > 8192) mib = 8192;
            cfg->large_file_limit = (uint64_t)mib * 1024ull * 1024ull;
        }
        else if (strcmp(key, "read_retries") == 0) {
            int r = atoi(val);
            if (r < 0) r = 0;
            if (r > 3) r = 3;
            cfg->read_retries = r;
        }
        else if (strcmp(key, "consistency_check") == 0) cfg->consistency_check = parse_bool(val, cfg->consistency_check) != 0;
        else if (strcmp(key, "chunk_mode") == 0) {
            int cm = atoi(val);
            if (cm < 0) cm = 0;
            if (cm > (int)CHUNK_1M) cm = (int)CHUNK_1M;
            cfg->chunk_mode = (ChunkMode)cm;
        }
        else if (strcmp(key, "skip_known_folders") == 0) cfg->skip_known_folders = parse_bool(val, cfg->skip_known_folders) != 0;
        else if (strcmp(key, "skip_media_exts") == 0) cfg->skip_media_exts = parse_bool(val, cfg->skip_media_exts) != 0;
        else if (strcmp(key, "deep_target") == 0) {
            int t = atoi(val);
            if (t < 0) t = 0;
            if (t > (int)SCAN_TARGET_CUSTOM_CFG) t = (int)SCAN_TARGET_CUSTOM_CFG;
            cfg->deep_target = (ScanTarget)t;
        }
        else if (strcmp(key, "custom_root") == 0) {
            snprintf(cfg->custom_root, sizeof(cfg->custom_root), "%s", val);
            if (!sanitize_custom_root(cfg->custom_root, sizeof(cfg->custom_root))) {
                set_default_custom_root(cfg->custom_root, sizeof(cfg->custom_root));
            }
        }
        else if (strcmp(key, "write_test") == 0) cfg->write_test = parse_bool(val, cfg->write_test) != 0;
        else if (strcmp(key, "list_root") == 0) cfg->list_root = parse_bool(val, cfg->list_root) != 0;
        else if (strcmp(key, "ui_top_margin") == 0) {
            int tm = atoi(val);
            if (tm < 0) tm = 0;
            if (tm > 2) tm = 2;
            ui->top_margin = tm;
        }
        else if (strcmp(key, "ui_compact_mode") == 0) ui->compact_mode = parse_bool(val, ui->compact_mode) != 0;
    }

    fclose(f);

    if (have_preset) {
        if (preset < (int)PRESET_CUSTOM) preset = (int)PRESET_CUSTOM;
        if (preset > (int)PRESET_FORENSICS) preset = (int)PRESET_FORENSICS;

        if (preset != (int)PRESET_CUSTOM) apply_preset(cfg, (PresetMode)preset);
        else cfg->preset = PRESET_CUSTOM;
    }

    log_push("INFO", "Config loaded: sdmc:/switch/sdcheck.cfg");
    return true;
}
