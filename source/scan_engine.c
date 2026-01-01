#include "scan_engine.h"
#include "util.h"
#include "log.h"

/* --------------------------------------------------------------------------
   CRC32
----------------------------------------------------------------------------*/
static uint32_t crc32_table[256];
static bool crc32_ready = false;

static void crc32_init(void) {
    if (crc32_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = true;
}

static uint32_t crc32_update(uint32_t crc, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) crc = crc32_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* --------------------------------------------------------------------------
   Small utilities
----------------------------------------------------------------------------*/
uint64_t scan_stats_elapsed_ms(const ScanStats* st, uint64_t now) {
    if (!st) return 0;
    uint64_t paused = st->paused_total_ms;
    if (st->paused && st->pause_start_ms) paused += (now - st->pause_start_ms);
    uint64_t base = (now > st->ui_start_ms) ? (now - st->ui_start_ms) : 0;
    return (base > paused) ? (base - paused) : 0;
}

static void err_push(ScanStats* st, const char* msg) {
    if (!st || !msg) return;
    int idx = st->err_ring_count % ERR_RING_MAX;
    snprintf(st->err_ring[idx], sizeof(st->err_ring[idx]), "%s", msg);
    st->err_ring_count++;
    log_push("ERROR", msg);
}

static void fail_push_unique(ScanStats* st, const char* path) {
    if (!st || !path || !path[0]) return;
    char t[256];
    snprintf(t, sizeof(t), "%.250s", path);

    for (int i = 0; i < st->fail_count; i++) {
        if (strcmp(st->fail_paths[i], t) == 0) return;
    }
    if (st->fail_count < FAIL_MAX) {
        snprintf(st->fail_paths[st->fail_count], sizeof(st->fail_paths[st->fail_count]), "%s", t);
        st->fail_count++;
    }
}

static void largest_update(ScanStats* st, const char* path, uint64_t size) {
    if (!st || !path || !path[0]) return;
    if (size == 0) return;

    int n = st->largest_count;
    if (n < 0) n = 0;

    int pos = -1;
    for (int i = 0; i < n; i++) {
        if (size > st->largest[i].size) { pos = i; break; }
    }
    if (pos < 0) {
        if (n < LARGEST_MAX) pos = n;
        else return;
    }

    if (n < LARGEST_MAX) n++;
    for (int i = n - 1; i > pos; i--) st->largest[i] = st->largest[i - 1];

    st->largest[pos].size = size;
    snprintf(st->largest[pos].path, sizeof(st->largest[pos].path), "%.250s", path);
    st->largest_count = n;
}

static void first_fail_capture(ScanStats* st, const char* kind, const char* path, uint64_t off, uint64_t bytes, int err, const char* note) {
    if (!st || st->first_fail_set) return;
    st->first_fail_set = true;
    snprintf(st->first_fail_kind, sizeof(st->first_fail_kind), "%s", kind ? kind : "FAIL");
    snprintf(st->first_fail_path, sizeof(st->first_fail_path), "%.250s", path ? path : "(unknown)");
    st->first_fail_off = off;
    st->first_fail_bytes = bytes;
    st->first_fail_errno = err;
    snprintf(st->first_fail_note, sizeof(st->first_fail_note), "%s", note ? note : "");
}

static void perf_record(ScanStats* st, uint64_t bytes, uint64_t dt_ms, uint64_t off, const char* path) {
    if (!st || bytes == 0) return;
    if (dt_ms == 0) dt_ms = 1;

    double secs = (double)dt_ms / 1000.0;
    double mib  = (double)bytes / 1048576.0;
    double mibs = (secs > 0.0) ? (mib / secs) : 0.0;

    st->perf_ops++;
    st->perf_bytes += bytes;

    int b = 4;
    if (mibs >= 60.0) b = 0;
    else if (mibs >= 30.0) b = 1;
    else if (mibs >= 10.0) b = 2;
    else if (mibs >= 1.0)  b = 3;
    else b = 4;
    st->perf_hist[b]++;

    if (mibs < 1.0 || dt_ms >= 500) {
        st->perf_stalls++;
        st->perf_stall_total_ms += dt_ms;
    }

    if (dt_ms > st->perf_longest_ms) {
        st->perf_longest_ms = dt_ms;
        st->perf_longest_mib_s = mibs;
        st->perf_longest_off = off;
        st->perf_longest_bytes = bytes;
        snprintf(st->perf_longest_path, sizeof(st->perf_longest_path), "%.250s", path ? path : "(unknown)");
    }
}

/* --------------------------------------------------------------------------
   Filters
----------------------------------------------------------------------------*/
static bool ends_with_ext_ci(const char* path, const char* ext) {
    if (!path || !ext) return false;
    size_t lp = strlen(path);
    size_t le = strlen(ext);
    if (lp < le) return false;
    const char* p = path + (lp - le);
    for (size_t i = 0; i < le; i++) {
        if (tolower((unsigned char)p[i]) != tolower((unsigned char)ext[i])) return false;
    }
    return true;
}

static bool path_contains_segment_ci(const char* path, const char* seg) {
    if (!path || !seg || !seg[0]) return false;

    char needle[64];
    snprintf(needle, sizeof(needle), "/%s/", seg);

    size_t ln = strlen(needle);
    size_t lp = strlen(path);
    for (size_t i = 0; i + ln <= lp; i++) {
        bool ok = true;
        for (size_t k = 0; k < ln; k++) {
            char a = tolower((unsigned char)path[i + k]);
            char b = tolower((unsigned char)needle[k]);
            if (a != b) { ok = false; break; }
        }
        if (ok) return true;
    }

    char needle2[64];
    snprintf(needle2, sizeof(needle2), "/%s", seg);
    ln = strlen(needle2);
    if (lp >= ln) {
        bool ok = true;
        for (size_t k = 0; k < ln; k++) {
            char a = tolower((unsigned char)path[lp - ln + k]);
            char b = tolower((unsigned char)needle2[k]);
            if (a != b) { ok = false; break; }
        }
        if (ok) return true;
    }

    char needle3[64];
    snprintf(needle3, sizeof(needle3), "sdmc:/%s", seg);
    ln = strlen(needle3);
    if (lp >= ln) {
        bool ok = true;
        for (size_t k = 0; k < ln; k++) {
            char a = tolower((unsigned char)path[k]);
            char b = tolower((unsigned char)needle3[k]);
            if (a != b) { ok = false; break; }
        }
        if (ok) return true;
    }

    return false;
}

static bool should_skip_dir(const char* path, const ScanConfig* cfg) {
    if (!cfg || !cfg->skip_known_folders) return false;
    /* Targeted Deep Check: do not skip 'known folders' when user explicitly targets them. */
    if (cfg->deep_target != SCAN_TARGET_ALL) return false;
    if (path_contains_segment_ci(path, "Nintendo")) return true;
    if (path_contains_segment_ci(path, "emuMMC")) return true;
    if (path_contains_segment_ci(path, "Emutendo")) return true;
    return false;
}

static bool should_skip_file(const char* path, const ScanConfig* cfg) {
    if (!cfg || !cfg->skip_media_exts) return false;

    const char* exts[] = {
        ".nsp", ".nsz", ".xci", ".xcz",
        ".mp4", ".mkv", ".avi", ".mov", ".webm",
        ".iso", ".bin", ".img",
        ".zip", ".7z", ".rar"
    };
    for (size_t i = 0; i < sizeof(exts)/sizeof(exts[0]); i++) {
        if (ends_with_ext_ci(path, exts[i])) return true;
    }
    return false;
}

/* --------------------------------------------------------------------------
   Buffer reuse (P1)
----------------------------------------------------------------------------*/
typedef struct {
    uint8_t* sample_buf;
    size_t sample_cap;
    uint8_t* chunk_buf;
    size_t chunk_cap;
} ScanBuffers;

static void scan_buffers_free(ScanBuffers* b) {
    if (!b) return;
    if (b->sample_buf) free(b->sample_buf);
    if (b->chunk_buf) free(b->chunk_buf);
    memset(b, 0, sizeof(*b));
}

static bool scan_buffers_init_default(ScanBuffers* b) {
    if (!b) return false;
    memset(b, 0, sizeof(*b));

    b->sample_cap = SAMPLE_REGION;
    b->chunk_cap = 1024u * 1024u;

    b->sample_buf = (uint8_t*)malloc(b->sample_cap);
    b->chunk_buf  = (uint8_t*)malloc(b->chunk_cap);

    if (!b->sample_buf || !b->chunk_buf) {
        scan_buffers_free(b);
        return false;
    }
    return true;
}

static bool ensure_chunk_cap(ScanBuffers* b, size_t need) {
    if (!b) return false;
    if (need <= b->chunk_cap) return true;
    uint8_t* nb = (uint8_t*)realloc(b->chunk_buf, need);
    if (!nb) return false;
    b->chunk_buf = nb;
    b->chunk_cap = need;
    return true;
}

/* --------------------------------------------------------------------------
   Read strategy (chunk, retry, consistency)
----------------------------------------------------------------------------*/
static size_t chunk_bytes_from_mode(ChunkMode m) {
    switch (m) {
        case CHUNK_128K: return 128u * 1024u;
        case CHUNK_256K: return 256u * 1024u;
        case CHUNK_512K: return 512u * 1024u;
        case CHUNK_1M:   return 1024u * 1024u;
        default: return 0;
    }
}

static size_t choose_chunk_auto(uint64_t file_size) {
    if (file_size >= (1ull * 1024ull * 1024ull * 1024ull)) return 1024u * 1024u;
    if (file_size >= (256ull * 1024ull * 1024ull)) return 512u * 1024u;
    if (file_size >= (64ull * 1024ull * 1024ull)) return 256u * 1024u;
    return 128u * 1024u;
}

static bool read_region_retry(FILE* f, uint64_t off, uint8_t* buf, size_t want, const ScanConfig* cfg, ScanStats* st, uint32_t* out_crc) {
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) {
        st->read_errors++;
        first_fail_capture(st, "SEEK", st->current_path, off, want, errno, "fseeko");
        err_push(st, "Seek error");
        return false;
    }

    int retries = cfg ? cfg->read_retries : 0;
    uint32_t crc = 0;

    for (int attempt = 0; attempt <= retries; attempt++) {
        errno = 0;
        uint64_t t0 = now_ms();
        size_t r = fread(buf, 1, want, f);
        uint64_t dt = now_ms() - t0;
        int e = errno;

        if (r > 0) {
            crc = crc32_update(crc, buf, r);
            st->bytes_read += r;
            st->current_done += r;
            perf_record(st, r, dt, off, st->current_path);
        }

        if (!ferror(f)) {
            if (out_crc) *out_crc = crc;
            return true;
        }

        clearerr(f);
        if (attempt < retries) {
            st->read_errors_transient++;
            svcSleepThread(30 * 1000 * 1000);
            continue;
        }

        st->read_errors++;
        first_fail_capture(st, "READ", st->current_path, off, want, e, "read_region");
        err_push(st, "Read error");
        return false;
    }

    return false;
}

static bool read_sample(FILE* f, uint64_t size, const ScanConfig* cfg, ScanStats* st, ScanBuffers* bufs, ScanUiUpdateFn ui_update, PadState* pad, uint32_t* out_crc) {
    if (!bufs || !bufs->sample_buf || bufs->sample_cap < SAMPLE_REGION) return false;

    uint8_t* buf = bufs->sample_buf;
    uint32_t crc_total = 0;

    size_t want = (size < SAMPLE_REGION) ? (size_t)size : (size_t)SAMPLE_REGION;
    uint32_t crc1 = 0;
    if (!read_region_retry(f, 0, buf, want, cfg, st, &crc1)) return false;
    crc_total ^= crc1;

    if (ui_update) ui_update(st, pad, false);
    if (st->cancelled) return false;

    if (cfg && cfg->consistency_check) {
        uint32_t crc1b = 0;
        st->current_done -= want;
        st->bytes_read -= want;
        if (!read_region_retry(f, 0, buf, want, cfg, st, &crc1b)) return false;
        st->current_done -= want;
        st->bytes_read -= want;
        if (crc1b != crc1) {
            st->consistency_errors++;
            first_fail_capture(st, "CONSIST", st->current_path, 0, SAMPLE_REGION, 0, "CRC mismatch");
            err_push(st, "Consistency mismatch (first region)");
            return false;
        }
    }

    if (size > SAMPLE_REGION) {
        uint64_t off = (size > SAMPLE_REGION) ? (size - SAMPLE_REGION) : 0;
        uint32_t crc2 = 0;
        if (!read_region_retry(f, off, buf, SAMPLE_REGION, cfg, st, &crc2)) return false;
        crc_total ^= crc2;

        if (ui_update) ui_update(st, pad, false);
        if (st->cancelled) return false;

        if (cfg && cfg->consistency_check) {
            uint32_t crc2b = 0;
            st->current_done -= SAMPLE_REGION;
            st->bytes_read -= SAMPLE_REGION;
            if (!read_region_retry(f, off, buf, SAMPLE_REGION, cfg, st, &crc2b)) return false;
            st->current_done -= SAMPLE_REGION;
            st->bytes_read -= SAMPLE_REGION;
            if (crc2b != crc2) {
                st->consistency_errors++;
                err_push(st, "Consistency mismatch (last region)");
                return false;
            }
        }
    }

    if (out_crc) *out_crc = crc_total;
    return !st->cancelled;
}

static bool read_full(FILE* f, uint64_t size, const ScanConfig* cfg, ScanStats* st, ScanBuffers* bufs, ScanUiUpdateFn ui_update, PadState* pad, uint32_t* out_crc) {
    size_t chunk = 256u * 1024u;
    if (cfg) {
        size_t fixed = chunk_bytes_from_mode(cfg->chunk_mode);
        chunk = fixed ? fixed : choose_chunk_auto(size);
    } else {
        chunk = choose_chunk_auto(size);
    }

    if (!bufs || !ensure_chunk_cap(bufs, chunk)) return false;
    uint8_t* buf = bufs->chunk_buf;

    uint32_t crc = 0;
    uint32_t first_crc = 0;
    bool first_crc_set = false;

    while (!st->cancelled) {
        errno = 0;
        uint64_t off0 = st->current_done;
        uint64_t t0 = now_ms();
        size_t r = fread(buf, 1, chunk, f);
        uint64_t dt = now_ms() - t0;
        if (r > 0) {
            perf_record(st, r, dt, off0, st->current_path);
            crc = crc32_update(crc, buf, r);
            if (!first_crc_set) {
                size_t a = (r < SAMPLE_REGION) ? r : SAMPLE_REGION;
                first_crc = crc32_update(0, buf, a);
                first_crc_set = true;
            }
            st->bytes_read += r;
            st->current_done += r;
        }

        if (r < chunk) {
            if (ferror(f)) {
                int last_e = errno;
                int retries = cfg ? cfg->read_retries : 0;
                bool ok = false;
                for (int attempt = 0; attempt < retries; attempt++) {
                    clearerr(f);
                    st->read_errors_transient++;
                    svcSleepThread(30 * 1000 * 1000);
                    errno = 0;
                    uint64_t off0b = st->current_done;
                    uint64_t t0b = now_ms();
                    r = fread(buf, 1, chunk, f);
                    uint64_t dtb = now_ms() - t0b;
                    int eb = errno;
                    last_e = eb;
                    if (r > 0) {
                        perf_record(st, r, dtb, off0b, st->current_path);
                        crc = crc32_update(crc, buf, r);
                        st->bytes_read += r;
                        st->current_done += r;
                    }
                    if (!ferror(f)) { ok = true; break; }
                }
                if (!ok) {
                    st->read_errors++;
                    first_fail_capture(st, "READ", st->current_path, st->current_done, chunk, last_e, "full read");
                    err_push(st, "Full: read error");
                    return false;
                }
                if (r < chunk && !ferror(f)) {
                    break;
                }
            } else {
                break;
            }
        }

        if (ui_update) ui_update(st, pad, false);
    }

    if (ui_update) ui_update(st, pad, true);

    if (cfg && cfg->consistency_check && first_crc_set && !st->cancelled) {
        size_t want = SAMPLE_REGION;
        if (fseeko(f, 0, SEEK_SET) == 0) {
            size_t rr = fread(buf, 1, want, f);
            if (!ferror(f) && rr > 0) {
                uint32_t c2 = crc32_update(0, buf, rr);
                if (c2 != first_crc) {
                    st->consistency_errors++;
                    first_fail_capture(st, "CONSIST", st->current_path, 0, SAMPLE_REGION, 0, "CRC mismatch");
                    err_push(st, "Consistency mismatch (first chunk)");
                    return false;
                }
            } else {
                int e = errno;
                clearerr(f);
                st->read_errors++;
                first_fail_capture(st, "READ", st->current_path, 0, SAMPLE_REGION, e, "consistency read");
                err_push(st, "Consistency check read failed");
                return false;
            }
        }
    }

    if (out_crc) *out_crc = crc;
    return !st->cancelled;
}

/* --------------------------------------------------------------------------
   Deep scan traversal
----------------------------------------------------------------------------*/
static bool scan_dir_recursive(const char* root, const char* path, int depth, const ScanConfig* cfg, ScanStats* st, PadState* pad, ScanUiUpdateFn ui_update, ScanBuffers* bufs) {
    (void)root;
    if (st->cancelled) return false;
    if (depth > 128) {
        st->path_errors++;
        err_push(st, "Maximum directory depth reached (possible loop)");
        return true;
    }

    if (should_skip_dir(path, cfg)) {
        st->skipped_dirs++;
        return true;
    }

    DIR* d = opendir(path);
    if (!d) {
        st->open_errors++;
        first_fail_capture(st, "OPEN_DIR", path, 0, 0, errno, "opendir");
        char msg[256];
        snprintf(msg, sizeof(msg), "opendir failed: %s (%.180s)", strerror(errno), path);
        err_push(st, msg);
        fail_push_unique(st, path);
        return true;
    }

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ui_update) ui_update(st, pad, false);
        if (st->cancelled) break;

        const char* name = ent->d_name;
        if (!name) continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char child[PATH_MAX_LOCAL];
        int n = snprintf(child, sizeof(child), "%s/%s", path, name);
        if (n <= 0 || (size_t)n >= sizeof(child)) {
            st->path_errors++;
            first_fail_capture(st, "PATH", path, 0, 0, 0, "Path too long");
            err_push(st, "Path too long (snprintf)");
            continue;
        }

        struct stat s;
        if (stat(child, &s) != 0) {
            st->stat_errors++;
            first_fail_capture(st, "STAT", child, 0, 0, errno, "stat");
            char msg[256];
            snprintf(msg, sizeof(msg), "stat failed: %s (%.180s)", strerror(errno), child);
            err_push(st, msg);
            fail_push_unique(st, child);
            continue;
        }

        if (S_ISDIR(s.st_mode)) {
            st->dirs_total++;

            if (should_skip_dir(child, cfg)) {
                st->skipped_dirs++;
                continue;
            }

            snprintf(st->current_path, sizeof(st->current_path), "%.250s", child);
            st->current_size = 0;
            st->current_planned = 0;
            st->current_done = 0;
            st->current_sample = false;

            if (!scan_dir_recursive(root, child, depth + 1, cfg, st, pad, ui_update, bufs)) break;

        } else if (S_ISREG(s.st_mode)) {
            st->files_total++;
            uint64_t fsize = (uint64_t)s.st_size;
            largest_update(st, child, fsize);

            if (should_skip_file(child, cfg)) {
                st->skipped_files++;
                continue;
            }

            bool sample = (!cfg->full_read && fsize > cfg->large_file_limit);

            snprintf(st->current_path, sizeof(st->current_path), "%.250s", child);
            st->current_size = fsize;
            st->current_done = 0;
            st->current_sample = sample;

            if (sample) {
                uint64_t want = SAMPLE_REGION;
                uint64_t p1 = (fsize < want) ? fsize : want;
                uint64_t p2 = (fsize > want) ? want : 0;
                st->current_planned = p1 + p2;
            } else {
                st->current_planned = fsize;
            }

            if (ui_update) ui_update(st, pad, true);
            if (st->cancelled) break;

            FILE* f = fopen(child, "rb");
            if (!f) {
                st->open_errors++;
                first_fail_capture(st, "OPEN_FILE", child, 0, 0, errno, "fopen");
                char msg[256];
                snprintf(msg, sizeof(msg), "fopen failed: %s (%.180s)", strerror(errno), child);
                err_push(st, msg);
                fail_push_unique(st, child);
                continue;
            }

            st->files_read++;
            uint32_t crc = 0;
            bool ok = sample ? read_sample(f, fsize, cfg, st, bufs, ui_update, pad, &crc)
                             : read_full  (f, fsize, cfg, st, bufs, ui_update, pad, &crc);
            fclose(f);

            if (!ok) {
                fail_push_unique(st, child);
                if (st->cancelled) break;
            }
        }
    }

    closedir(d);
    return !st->cancelled;
}

bool scan_engine_run(const char* root, const ScanConfig* cfg, ScanStats* st, PadState* pad, ScanUiUpdateFn ui_update) {
    if (!root || !cfg || !st) return false;

    crc32_init();

    ScanBuffers bufs;
    if (!scan_buffers_init_default(&bufs)) {
        err_push(st, "Out of memory (scan buffers)");
        return false;
    }

    bool ok = scan_dir_recursive(root, root, 0, cfg, st, pad, ui_update, &bufs);

    scan_buffers_free(&bufs);
    return ok;
}
