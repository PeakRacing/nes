/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * ROM corpus regression runner.
 *
 * The corpus is discovered by scanning a directory (default <root>/rom) for
 * *.nes images; a parent directory named "mapperNNN" records the expected
 * mapper. Nothing is hand-maintained, so the corpus can grow freely:
 *
 *   - an image that is not in the baseline is reported as NEW and never fails
 *   - a baseline entry whose file is gone stays in the baseline as MISSING
 *   - only "was rendering -> now blank/load failure" counts as a regression
 *
 * The baseline (test/baseline/corpus.csv) is keyed by SHA-256, so renaming or
 * moving a ROM does not invalidate it, and it contains no ROM data.
 */
#include "test.h"

#if defined(_MSC_VER)
#include <io.h>
#include <direct.h>
#define CORPUS_USE_FINDFIRST 1
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define CORPUS_MAX_ENTRIES 4096
#define CORPUS_LABEL_MAX 400
#define CORPUS_PATH_MAX 600

typedef enum {
    ST_NEW = 0, ST_OK, ST_REGRESSED, ST_SUSPECT, ST_IMPROVED, ST_KNOWN_BAD,
    ST_MISSING, ST_STALE_FRAMES, ST_HASH_DIFF, ST_LOAD_FAIL
} corpus_status_t;

typedef struct {
    char path[CORPUS_PATH_MAX];
    char label[CORPUS_LABEL_MAX];
    char sha256[65];
    int dir_mapper;
    int header_mapper;
    int effective_mapper;
    unsigned frames;
    int verdict;
    uint32_t hash_chain;
    unsigned first_render;
    corpus_status_t status;
} corpus_entry_t;

static corpus_entry_t* corpus_entries;
static size_t corpus_entry_count;
static size_t corpus_entry_cap;

/* ------------------------------------------------------------------ */
/* SHA-256                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    size_t used;
} sha256_ctx_t;

static const uint32_t sha256_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

#define SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(sha256_ctx_t* ctx, const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = SHA256_ROTR(w[i - 15], 7) ^ SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = SHA256_ROTR(w[i - 2], 17) ^ SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = SHA256_ROTR(e, 6) ^ SHA256_ROTR(e, 11) ^ SHA256_ROTR(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + s1 + ch + sha256_k[i] + w[i];
        const uint32_t s0 = SHA256_ROTR(a, 2) ^ SHA256_ROTR(a, 13) ^ SHA256_ROTR(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t* ctx) {
    static const uint32_t init[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    memcpy(ctx->state, init, sizeof(init));
    ctx->bits = 0;
    ctx->used = 0;
}

static void sha256_update(sha256_ctx_t* ctx, const uint8_t* data, size_t size) {
    ctx->bits += (uint64_t)size * 8u;
    while (size > 0) {
        const size_t room = 64u - ctx->used;
        const size_t take = (size < room) ? size : room;
        memcpy(ctx->buffer + ctx->used, data, take);
        ctx->used += take;
        data += take;
        size -= take;
        if (ctx->used == 64u) {
            sha256_block(ctx, ctx->buffer);
            ctx->used = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t* ctx, char* out_hex) {
    static const char hex[] = "0123456789abcdef";
    const uint64_t bits = ctx->bits;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    ctx->bits = bits;
    uint8_t zero = 0;
    while (ctx->used != 56u) {
        sha256_update(ctx, &zero, 1);
        ctx->bits = bits;
    }
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = (uint8_t)(bits >> (56 - i * 8));
    sha256_update(ctx, length, sizeof(length));
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 4; ++j) {
            const uint8_t byte = (uint8_t)(ctx->state[i] >> (24 - j * 8));
            *out_hex++ = hex[byte >> 4];
            *out_hex++ = hex[byte & 0x0F];
        }
    }
    *out_hex = 0;
}

static int corpus_file_sha256(const char* path, char* out_hex) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return 0;
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    uint8_t buffer[8192];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) sha256_update(&ctx, buffer, n);
    fclose(f);
    sha256_final(&ctx, out_hex);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void corpus_out_path(char* buf, size_t size, const char* name) {
    char rel[CORPUS_PATH_MAX];
    snprintf(rel, sizeof(rel), "test/out/%s", name);
    test_path(buf, size, rel);
}

/* Create the directory holding `file` when it does not exist yet (best effort). */
static void corpus_ensure_dir(const char* file) {
    char dir[CORPUS_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", file);
    char* slash = strrchr(dir, '/');
    char* backslash = strrchr(dir, '\\');
    char* cut = slash;
    if (backslash != NULL && (cut == NULL || backslash > cut)) cut = backslash;
    if (cut == NULL) return;
    *cut = 0;
#if defined(_MSC_VER)
    _mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
}

static void corpus_baseline_path(char* buf, size_t size) {
    test_path(buf, size, "test/baseline/corpus.csv");
}

static int corpus_has_nes_suffix(const char* name) {
    const size_t len = strlen(name);
    if (len < 4) return 0;
    const char* ext = name + len - 4;
    return (ext[0] == '.') &&
           (ext[1] == 'n' || ext[1] == 'N') &&
           (ext[2] == 'e' || ext[2] == 'E') &&
           (ext[3] == 's' || ext[3] == 'S');
}

static int corpus_dir_mapper(const char* dir_name) {
    if (strncmp(dir_name, "mapper", 6) != 0) return -1;
    const char* digits = dir_name + 6;
    if (*digits == 0) return -1;
    int value = 0;
    for (const char* p = digits; *p != 0; ++p) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10 + (*p - '0');
        if (value > 4095) return -1;
    }
    return value;
}

static const char* corpus_base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* best = path;
    if (slash != NULL && slash + 1 > best) best = slash + 1;
    if (backslash != NULL && backslash + 1 > best) best = backslash + 1;
    return best;
}

/* Directory that contains the file, i.e. the "mapperNNN" folder. */
static void corpus_parent_name(const char* path, char* out, size_t size) {
    char copy[CORPUS_PATH_MAX];
    snprintf(copy, sizeof(copy), "%s", path);
    char* slash = strrchr(copy, '/');
    char* backslash = strrchr(copy, '\\');
    char* cut = slash;
    if (backslash != NULL && (cut == NULL || backslash > cut)) cut = backslash;
    if (cut == NULL) { snprintf(out, size, ""); return; }
    *cut = 0;
    const char* dir = corpus_base_name(copy);
    snprintf(out, size, "%s", dir);
}

static void corpus_sanitize_label(char* label) {
    for (char* p = label; *p != 0; ++p) {
        if (*p == ',' || *p == '\n' || *p == '\r' || *p == '\t') *p = '_';
    }
}

static void corpus_add(const char* path) {
    if (corpus_entry_count >= CORPUS_MAX_ENTRIES) return;
    if (corpus_entry_count == corpus_entry_cap) {
        const size_t next = (corpus_entry_cap == 0) ? 256 : corpus_entry_cap * 2;
        corpus_entry_t* grown = (corpus_entry_t*)realloc(corpus_entries, next * sizeof(corpus_entry_t));
        if (grown == NULL) return;
        corpus_entries = grown;
        corpus_entry_cap = next;
    }
    corpus_entry_t* e = &corpus_entries[corpus_entry_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    /* label = path relative to the repository root when possible */
    const char* root = test_root();
    const size_t root_len = strlen(root);
    const char* label = path;
    if (strncmp(path, root, root_len) == 0) {
        label = path + root_len;
        while (*label == '/' || *label == '\\') ++label;
    }
    snprintf(e->label, sizeof(e->label), "%s", label);
    corpus_sanitize_label(e->label);
    char parent[128];
    corpus_parent_name(path, parent, sizeof(parent));
    e->dir_mapper = corpus_dir_mapper(parent);
    e->frames = test_get_options()->frames;
}

static int corpus_entry_compare(const void* a, const void* b) {
    const corpus_entry_t* ea = (const corpus_entry_t*)a;
    const corpus_entry_t* eb = (const corpus_entry_t*)b;
    return strcmp(ea->label, eb->label);
}

static void corpus_scan(const char* dir) {
#if defined(CORPUS_USE_FINDFIRST)
    char pattern[CORPUS_PATH_MAX];
    snprintf(pattern, sizeof(pattern), "%s/*", dir);
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern, &fd);
    if (handle == -1) return;
    do {
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) continue;
        char child[CORPUS_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", dir, fd.name);
        if ((fd.attrib & _A_SUBDIR) != 0) {
            corpus_scan(child);
        } else if (corpus_has_nes_suffix(fd.name)) {
            corpus_add(child);
        }
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    DIR* handle = opendir(dir);
    if (handle == NULL) return;
    struct dirent* entry;
    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char child[CORPUS_PATH_MAX];
        snprintf(child, sizeof(child), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            corpus_scan(child);
        } else if (corpus_has_nes_suffix(entry->d_name)) {
            corpus_add(child);
        }
    }
    closedir(handle);
#endif
}

static int corpus_read_header_mapper(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return -1;
    uint8_t header[16];
    const size_t got = fread(header, 1, sizeof(header), f);
    fclose(f);
    if (got < 16 || memcmp(header, "NES\x1a", 4) != 0) return -1;
    if (((header[7] >> 2) & 0x03u) == 2u) {   /* NES 2.0 */
        return (int)(((header[7] >> 4) & 0x0Fu) | ((header[6] >> 4) << 4) | ((header[8] & 0x0Fu) << 8));
    }
    if (header[12] | header[13] | header[14] | header[15]) return (int)(header[6] >> 4);
    return (int)((header[6] >> 4) | (header[7] & 0xF0u));
}

/* ------------------------------------------------------------------ */
/* Running one image                                                   */
/* ------------------------------------------------------------------ */

static const char* corpus_verdict_name(int verdict) {
    switch (verdict) {
        case CORPUS_OK: return "ok";
        case CORPUS_SUSPECT: return "suspect";
        case CORPUS_BLANK: return "blank";
        case CORPUS_LOAD_FAIL: return "load_fail";
        default: return "?";
    }
}

static int corpus_verdict_from_name(const char* name) {
    if (strcmp(name, "ok") == 0) return CORPUS_OK;
    if (strcmp(name, "suspect") == 0) return CORPUS_SUSPECT;
    if (strcmp(name, "blank") == 0) return CORPUS_BLANK;
    return CORPUS_LOAD_FAIL;
}

static int corpus_run_entry(corpus_entry_t* e) {
    if (!corpus_file_sha256(e->path, e->sha256)) return 0;
    e->header_mapper = corpus_read_header_mapper(e->path);
    e->effective_mapper = -1;
    e->verdict = CORPUS_LOAD_FAIL;
    e->hash_chain = 0;
    e->first_render = 0;
    e->status = ST_LOAD_FAIL;

    nes_t* nes = nes_init();
    if (nes == NULL) return 0;
    if (nes_load_file(nes, e->path) != NES_OK) {
        nes_deinit(nes);
        return 1;                       /* verdict stays load_fail */
    }
    e->effective_mapper = (int)nes->nes_rom.mapper_number;
    if (e->frames > 0) {
        test_probe_begin();
        nes_test_run_frames(nes, e->frames);
        const test_frame_probe_t* probe = test_probe_end();
        e->hash_chain = probe->hash_chain;
        e->first_render = probe->first_render_frame;
        if (!probe->render_seen) e->verdict = CORPUS_BLANK;
        else e->verdict = probe->nonuniform_seen ? CORPUS_OK : CORPUS_SUSPECT;
    }
    nes_unload_file(nes);
    nes_deinit(nes);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Baseline                                                            */
/* ------------------------------------------------------------------ */

static int corpus_parse_line(char* line, corpus_entry_t* e) {
    /* status,verdict,sha256,label,dir_mapper,header_mapper,effective_mapper,frames,hash_chain,first_render */
    char* fields[10];
    size_t count = 0;
    char* cursor = line;
    while (count < 10 && cursor != NULL) {
        fields[count++] = cursor;
        char* comma = strchr(cursor, ',');
        if (comma != NULL) { *comma = 0; cursor = comma + 1; } else cursor = NULL;
    }
    if (count < 10) return 0;
    memset(e, 0, sizeof(*e));
    e->status = ST_OK;
    snprintf(e->sha256, sizeof(e->sha256), "%s", fields[2]);
    if (strcmp(fields[2], "sha256") == 0) return 0;         /* header row */
    if (strlen(e->sha256) != 64) return 0;
    snprintf(e->label, sizeof(e->label), "%s", fields[3]);
    e->verdict = corpus_verdict_from_name(fields[1]);
    e->dir_mapper = atoi(fields[4]);
    e->header_mapper = atoi(fields[5]);
    e->effective_mapper = atoi(fields[6]);
    e->frames = (unsigned)atoi(fields[7]);
    e->hash_chain = (uint32_t)strtoul(fields[8], NULL, 10);
    e->first_render = (unsigned)atoi(fields[9]);
    return 1;
}

static size_t corpus_load_csv(const char* path, corpus_entry_t** out) {
    FILE* f = fopen(path, "r");
    *out = NULL;
    if (f == NULL) return 0;
    size_t cap = 64, count = 0;
    corpus_entry_t* list = (corpus_entry_t*)calloc(cap, sizeof(corpus_entry_t));
    if (list == NULL) { fclose(f); return 0; }
    char line[1024];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (count == cap) {
            corpus_entry_t* grown = (corpus_entry_t*)realloc(list, cap * 2 * sizeof(corpus_entry_t));
            if (grown == NULL) break;
            list = grown;
            cap *= 2;
        }
        if (corpus_parse_line(line, &list[count])) ++count;
    }
    fclose(f);
    *out = list;
    return count;
}

static const corpus_entry_t* corpus_find(const corpus_entry_t* list, size_t count, const char* sha256) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(list[i].sha256, sha256) == 0) return &list[i];
    }
    return NULL;
}

static const char* corpus_status_name(corpus_status_t status) {
    switch (status) {
        case ST_NEW: return "NEW";
        case ST_OK: return "OK";
        case ST_REGRESSED: return "REGRESSED";
        case ST_SUSPECT: return "SUSPECT";
        case ST_IMPROVED: return "IMPROVED";
        case ST_KNOWN_BAD: return "KNOWN_BAD";
        case ST_MISSING: return "MISSING";
        case ST_STALE_FRAMES: return "STALE_FRAMES";
        case ST_HASH_DIFF: return "HASH_DIFF";
        case ST_LOAD_FAIL: return "LOAD_FAIL";
        default: return "?";
    }
}

static void corpus_compare(corpus_entry_t* e, const corpus_entry_t* baseline, size_t baseline_count) {
    const corpus_entry_t* base = corpus_find(baseline, baseline_count, e->sha256);
    const int base_ok = base != NULL && (base->verdict == CORPUS_OK || base->verdict == CORPUS_SUSPECT);
    const int now_bad = e->verdict == CORPUS_BLANK || e->verdict == CORPUS_LOAD_FAIL;
    if (base == NULL) {
        e->status = ST_NEW;
    } else if (base->frames != e->frames) {
        e->status = ST_STALE_FRAMES;
    } else if (base_ok && now_bad) {
        e->status = ST_REGRESSED;
    } else if (base->verdict == CORPUS_OK && e->verdict == CORPUS_SUSPECT) {
        e->status = ST_SUSPECT;
    } else if (base->verdict != CORPUS_OK && e->verdict == CORPUS_OK) {
        e->status = ST_IMPROVED;
    } else if (now_bad) {
        e->status = ST_KNOWN_BAD;
    } else if (test_get_options()->strict && base->hash_chain != e->hash_chain) {
        e->status = ST_HASH_DIFF;
    } else {
        e->status = ST_OK;
    }
}

static void corpus_write_baseline(const corpus_entry_t* fresh, size_t fresh_count) {
    char path[CORPUS_PATH_MAX];
    corpus_baseline_path(path, sizeof(path));
    corpus_ensure_dir(path);
    corpus_entry_t* old = NULL;
    const size_t old_count = corpus_load_csv(path, &old);

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        printf("    warning: cannot write baseline %s\n", path);
        free(old);
        return;
    }
    fprintf(f, "# status,verdict,sha256,label,dir_mapper,header_mapper,effective_mapper,frames,hash_chain,first_render\n");
    for (size_t i = 0; i < fresh_count; ++i) {
        fprintf(f, "BASE,%s,%s,%s,%d,%d,%d,%u,%u,%u\n",
                corpus_verdict_name(fresh[i].verdict), fresh[i].sha256, fresh[i].label,
                fresh[i].dir_mapper, fresh[i].header_mapper, fresh[i].effective_mapper,
                fresh[i].frames, fresh[i].hash_chain, fresh[i].first_render);
    }
    /* Keep entries whose image is not present right now: the corpus may live
     * on another machine, but the baseline must survive. */
    for (size_t i = 0; i < old_count; ++i) {
        int seen = 0;
        for (size_t j = 0; j < fresh_count && !seen; ++j) {
            if (strcmp(old[i].sha256, fresh[j].sha256) == 0) seen = 1;
        }
        if (!seen) {
            fprintf(f, "BASE,%s,%s,%s,%d,%d,%d,%u,%u,%u\n",
                    corpus_verdict_name(old[i].verdict), old[i].sha256, old[i].label,
                    old[i].dir_mapper, old[i].header_mapper, old[i].effective_mapper,
                    old[i].frames, old[i].hash_chain, old[i].first_render);
        }
    }
    fclose(f);
    free(old);
    printf("    baseline updated: %s\n", path);
}

/* ------------------------------------------------------------------ */
/* Reports                                                             */
/* ------------------------------------------------------------------ */

static void corpus_write_results(const char* path, const corpus_entry_t* list, size_t count) {
    corpus_ensure_dir(path);
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        printf("    warning: cannot write %s\n", path);
        return;
    }
    fprintf(f, "status,verdict,sha256,label,dir_mapper,header_mapper,effective_mapper,frames,hash_chain,first_render\n");
    for (size_t i = 0; i < count; ++i) {
        fprintf(f, "%s,%s,%s,%s,%d,%d,%d,%u,%u,%u\n", corpus_status_name(list[i].status),
                corpus_verdict_name(list[i].verdict), list[i].sha256, list[i].label,
                list[i].dir_mapper, list[i].header_mapper, list[i].effective_mapper,
                list[i].frames, list[i].hash_chain, list[i].first_render);
    }
    fclose(f);
}

static void corpus_write_coverage(const corpus_entry_t* list, size_t count) {
    char path[CORPUS_PATH_MAX];
    corpus_out_path(path, sizeof(path), "mapper_coverage.csv");
    corpus_ensure_dir(path);
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        printf("    warning: cannot write %s\n", path);
        return;
    }
    fprintf(f, "mapper,supported,roms,ok,suspect,blank,load_fail,best_verdict\n");
    for (int mapper = 0; mapper < 256; ++mapper) {
        int roms = 0, ok = 0, suspect = 0, blank = 0, load_fail = 0;
        for (size_t i = 0; i < count; ++i) {
            const int used = (list[i].effective_mapper >= 0) ? list[i].effective_mapper
                                                             : list[i].dir_mapper;
            if (used != mapper) continue;
            ++roms;
            switch (list[i].verdict) {
                case CORPUS_OK: ++ok; break;
                case CORPUS_SUSPECT: ++suspect; break;
                case CORPUS_BLANK: ++blank; break;
                default: ++load_fail; break;
            }
        }
        const int supported = test_mapper_supported((uint16_t)mapper);
        if (!supported && roms == 0) continue;
        const char* best = (ok > 0) ? "ok" : (suspect > 0) ? "suspect" : (roms > 0) ? "none" : "no-rom";
        fprintf(f, "%d,%d,%d,%d,%d,%d,%d,%s\n", mapper, supported, roms, ok, suspect, blank, load_fail, best);
    }
    fclose(f);
}

static void corpus_write_mismatch(const corpus_entry_t* list, size_t count) {
    char path[CORPUS_PATH_MAX];
    corpus_out_path(path, sizeof(path), "mapper_mismatch.csv");
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        printf("    warning: cannot write %s\n", path);
        return;
    }
    fprintf(f, "label,sha256,dir_mapper,header_mapper,effective_mapper,verdict\n");
    int rows = 0;
    for (size_t i = 0; i < count; ++i) {
        if (list[i].dir_mapper < 0 || list[i].effective_mapper < 0) continue;
        if (list[i].dir_mapper == list[i].effective_mapper) continue;
        fprintf(f, "%s,%s,%d,%d,%d,%s\n", list[i].label, list[i].sha256, list[i].dir_mapper,
                list[i].header_mapper, list[i].effective_mapper,
                corpus_verdict_name(list[i].verdict));
        ++rows;
    }
    fclose(f);
    if (rows > 0) {
        printf("    note: %d image(s) load under a different mapper than their folder "
               "(candidates for src/nes_rom.c romdb), see test/out/mapper_mismatch.csv\n", rows);
    }
}

static void corpus_write_report(const corpus_entry_t* list, size_t count) {
    char path[CORPUS_PATH_MAX];
    corpus_out_path(path, sizeof(path), "mapper_report.md");
    FILE* f = fopen(path, "w");
    if (f == NULL) {
        printf("    warning: cannot write %s\n", path);
        return;
    }
    int ok_mapper[256];
    int any_mapper[256];
    for (int i = 0; i < 256; ++i) { ok_mapper[i] = 0; any_mapper[i] = 0; }
    for (size_t i = 0; i < count; ++i) {
        const int used = (list[i].effective_mapper >= 0) ? list[i].effective_mapper : list[i].dir_mapper;
        if (used < 0 || used > 255) continue;
        any_mapper[used] = 1;
        if (list[i].verdict == CORPUS_OK) ok_mapper[used] = 1;
    }
    fprintf(f, "## mapper verdict from rom/ corpus\n\n");
    fprintf(f, "Verified playable (at least one image reaches rendering):\n\n\t");
    int column = 0;
    for (int i = 0; i < 256; ++i) {
        if (!ok_mapper[i]) continue;
        fprintf(f, "%d, ", i);
        if (++column % 16 == 0) fprintf(f, "\n\t");
    }
    fprintf(f, "\n\nImages present but never rendering:\n\n\t");
    column = 0;
    for (int i = 0; i < 256; ++i) {
        if (ok_mapper[i] || !any_mapper[i]) continue;
        fprintf(f, "%d, ", i);
        if (++column % 16 == 0) fprintf(f, "\n\t");
    }
    fprintf(f, "\n\nSupported by the core but without any image in the corpus:\n\n\t");
    column = 0;
    for (int i = 0; i < 256; ++i) {
        if (any_mapper[i] || !test_mapper_supported((uint16_t)i)) continue;
        fprintf(f, "%d, ", i);
        if (++column % 16 == 0) fprintf(f, "\n\t");
    }
    fprintf(f, "\n");
    fclose(f);
    printf("    reports: test/out/corpus.csv, mapper_coverage.csv, mapper_mismatch.csv, mapper_report.md\n");
}

static int corpus_summarize(const corpus_entry_t* list, size_t count) {
    int counts[16];
    int verdicts[CORPUS_VERDICT_COUNT];
    memset(counts, 0, sizeof(counts));
    memset(verdicts, 0, sizeof(verdicts));
    for (size_t i = 0; i < count; ++i) {
        counts[list[i].status]++;
        if (list[i].verdict >= 0 && list[i].verdict < CORPUS_VERDICT_COUNT) verdicts[list[i].verdict]++;
    }
    printf("    %s: %zu images | ok %d suspect %d blank %d load_fail %d\n",
           test_rom_dir(), count, verdicts[CORPUS_OK], verdicts[CORPUS_SUSPECT],
           verdicts[CORPUS_BLANK], verdicts[CORPUS_LOAD_FAIL]);
    printf("    vs baseline: NEW %d OK %d REGRESSED %d IMPROVED %d KNOWN_BAD %d "
           "SUSPECT %d STALE_FRAMES %d HASH_DIFF %d\n",
           counts[ST_NEW], counts[ST_OK], counts[ST_REGRESSED],
           counts[ST_IMPROVED], counts[ST_KNOWN_BAD], counts[ST_SUSPECT],
           counts[ST_STALE_FRAMES], counts[ST_HASH_DIFF]);

    int failed = 0;
    for (size_t i = 0; i < count; ++i) {
        if (list[i].status == ST_REGRESSED || list[i].status == ST_HASH_DIFF) {
            printf("    %s: %s (now %s)\n", corpus_status_name(list[i].status), list[i].label,
                   corpus_verdict_name(list[i].verdict));
            ++failed;
        }
    }
    return failed;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                        */
/* ------------------------------------------------------------------ */

static void corpus_free_entries(void) {
    free(corpus_entries);
    corpus_entries = NULL;
    corpus_entry_count = 0;
    corpus_entry_cap = 0;
}

int test_corpus_case(void) {
    const test_options_t* options = test_get_options();
    corpus_scan(test_rom_dir());
    if (corpus_entry_count == 0) {
        TEST_SKIP_MSG("no .nes images found (use --rom-dir)");
    }
    qsort(corpus_entries, corpus_entry_count, sizeof(corpus_entry_t), corpus_entry_compare);

    /* Optional trimming so a growing corpus stays usable in one sitting. */
    size_t selected = 0;
    for (size_t i = 0; i < corpus_entry_count; ++i) {
        corpus_entry_t* e = &corpus_entries[i];
        if (options->mapper_filter >= 0 && e->dir_mapper != options->mapper_filter) continue;
        if (options->max_roms > 0 && selected >= options->max_roms) break;
        if (selected != i) corpus_entries[selected] = *e;
        ++selected;
    }
    corpus_entry_count = selected;
    if (corpus_entry_count == 0) {
        TEST_SKIP_MSG("no corpus image matches the mapper/max-roms filter");
    }

    printf("    running %zu image(s) from %s, %u frame(s) each\n",
           corpus_entry_count, test_rom_dir(), options->frames);
    size_t ran = 0;
    for (size_t i = 0; i < corpus_entry_count; ++i) {
        corpus_entry_t* e = &corpus_entries[i];
        printf("    RUN %s\n", e->label);
        fflush(stdout);
        if (!corpus_run_entry(e)) {
            test_record_failure(__FILE__, __LINE__, "corpus image run", "result", "io error");
            corpus_free_entries();
            return TEST_FAIL;
        }
        ++ran;
    }
    printf("    ran %zu image(s)\n", ran);

    char baseline_path[CORPUS_PATH_MAX];
    corpus_baseline_path(baseline_path, sizeof(baseline_path));
    corpus_entry_t* baseline = NULL;
    const size_t baseline_count = corpus_load_csv(baseline_path, &baseline);
    for (size_t i = 0; i < corpus_entry_count; ++i) {
        corpus_compare(&corpus_entries[i], baseline, baseline_count);
    }
    if (baseline_count == 0) {
        printf("    no baseline at %s (all images are NEW)\n", baseline_path);
    } else {
        /* Baseline entries whose image is not on this machine stay valid; tell
         * the user how many of them were not exercised by this run. */
        size_t missing = 0;
        for (size_t i = 0; i < baseline_count; ++i) {
            int found = 0;
            for (size_t j = 0; j < corpus_entry_count && !found; ++j) {
                if (strcmp(baseline[i].sha256, corpus_entries[j].sha256) == 0) found = 1;
            }
            if (!found) ++missing;
        }
        printf("    baseline entries: %zu, not present locally: %zu\n", baseline_count, missing);
    }
    free(baseline);

    char results_path[CORPUS_PATH_MAX];
    corpus_out_path(results_path, sizeof(results_path), "corpus.csv");
    corpus_write_results(results_path, corpus_entries, corpus_entry_count);
    corpus_write_coverage(corpus_entries, corpus_entry_count);
    corpus_write_mismatch(corpus_entries, corpus_entry_count);
    corpus_write_report(corpus_entries, corpus_entry_count);
    if (options->update_baseline) {
        corpus_write_baseline(corpus_entries, corpus_entry_count);
    }
    const int failed = corpus_summarize(corpus_entries, corpus_entry_count);
    corpus_free_entries();
    return failed == 0 ? TEST_PASS : TEST_FAIL;
}

int test_corpus_single(const char* rom_path) {
    const test_options_t* options = test_get_options();
    corpus_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    snprintf(entry.path, sizeof(entry.path), "%s", rom_path);
    snprintf(entry.label, sizeof(entry.label), "%s", rom_path);
    corpus_sanitize_label(entry.label);
    {
        char parent[128];
        corpus_parent_name(rom_path, parent, sizeof(parent));
        entry.dir_mapper = corpus_dir_mapper(parent);
    }
    entry.frames = options->frames;
    if (!corpus_run_entry(&entry)) {
        printf("ERROR: cannot read %s\n", rom_path);
        return 1;
    }
    char baseline_path[CORPUS_PATH_MAX];
    corpus_baseline_path(baseline_path, sizeof(baseline_path));
    corpus_entry_t* baseline = NULL;
    const size_t baseline_count = corpus_load_csv(baseline_path, &baseline);
    corpus_compare(&entry, baseline, baseline_count);
    free(baseline);

    printf("%s,%s,%s,%s,%d,%d,%d,%u,%u,%u\n", corpus_status_name(entry.status),
           corpus_verdict_name(entry.verdict), entry.sha256, entry.label,
           entry.dir_mapper, entry.header_mapper, entry.effective_mapper,
           entry.frames, entry.hash_chain, entry.first_render);
    if (options->update_baseline) {
        corpus_entry_t* single = &entry;
        corpus_write_baseline(single, 1);
    }
    return (entry.status == ST_REGRESSED || entry.status == ST_HASH_DIFF) ? 1 : 0;
}

int test_corpus_analyze(const char* results_csv) {
    corpus_entry_t* list = NULL;
    const size_t count = corpus_load_csv(results_csv, &list);
    if (count == 0) {
        printf("ERROR: no usable rows in %s\n", results_csv);
        free(list);
        return 1;
    }
    char baseline_path[CORPUS_PATH_MAX];
    corpus_baseline_path(baseline_path, sizeof(baseline_path));
    corpus_entry_t* baseline = NULL;
    const size_t baseline_count = corpus_load_csv(baseline_path, &baseline);
    for (size_t i = 0; i < count; ++i) corpus_compare(&list[i], baseline, baseline_count);
    free(baseline);

    char report_path[CORPUS_PATH_MAX];
    corpus_out_path(report_path, sizeof(report_path), "corpus.csv");
    corpus_write_results(report_path, list, count);
    corpus_write_coverage(list, count);
    corpus_write_mismatch(list, count);
    corpus_write_report(list, count);
    const int failed = corpus_summarize(list, count);
    free(list);
    return failed == 0 ? 0 : 1;
}
