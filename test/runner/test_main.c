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
#include "test.h"
#include "test_cases.h"
#include <time.h>

static int matches(const char* filter, const char* group, const char* name) {
    if (filter == NULL || filter[0] == 0 || strcmp(filter, "all") == 0) {
        /* The corpus run needs local ROM files and grows without bound, so it
         * stays opt-in: use --filter corpus. */
        return strcmp(group, "corpus") != 0;
    }
    return strstr(group, filter) != NULL || strstr(name, filter) != NULL;
}

static unsigned parse_u32(const char* text, unsigned fallback) {
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text) return fallback;
    return (unsigned)value;
}

int main(int argc, char** argv) {
    const char* filter = "all";
    test_options_t options;
    memset(&options, 0, sizeof(options));
    options.frames = 180;
    options.mapper_filter = -1;
    options.timeout_ms = 60000;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
        else if (strcmp(argv[i], "--report") == 0 && i + 1 < argc) options.report = argv[++i];
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) options.timeout_ms = parse_u32(argv[++i], 60) * 1000u;
        else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) options.root = argv[++i];
        else if (strcmp(argv[i], "--rom-dir") == 0 && i + 1 < argc) options.rom_dir = argv[++i];
        else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) options.single_rom = argv[++i];
        else if (strcmp(argv[i], "--analyze") == 0 && i + 1 < argc) options.analyze = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) options.frames = parse_u32(argv[++i], 180);
        else if (strcmp(argv[i], "--max-roms") == 0 && i + 1 < argc) options.max_roms = parse_u32(argv[++i], 0);
        else if (strcmp(argv[i], "--mapper") == 0 && i + 1 < argc) options.mapper_filter = (int)parse_u32(argv[++i], 0);
        else if (strcmp(argv[i], "--update-baseline") == 0) options.update_baseline = 1;
        else if (strcmp(argv[i], "--strict") == 0) options.strict = 1;
        else if (strcmp(argv[i], "--list") == 0) filter = "__list__";
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: nes-tests [--filter all|cpu|ppu|apu|rom|mapper|stress|corpus] [--report FILE]\n"
                   "                 [--timeout SEC] [--root DIR] [--list]\n"
                   "                 [--rom-dir DIR] [--frames N] [--mapper N] [--max-roms N]\n"
                   "                 [--update-baseline] [--strict]\n"
                   "                 [--rom FILE] [--analyze RESULTS.csv]\n");
            return 0;
        }
    }
    test_set_options(&options);
    test_platform_install();

    if (options.single_rom != NULL) {
        int rc = test_corpus_single(options.single_rom);
        test_platform_uninstall();
        return rc;
    }
    if (options.analyze != NULL) {
        int rc = test_corpus_analyze(options.analyze);
        test_platform_uninstall();
        return rc;
    }

    size_t count = 0;
    const nes_test_case_t* cases = test_cases(&count);

    if (strcmp(filter, "__list__") == 0) {
        for (size_t i = 0; i < count; ++i) printf("%s/%s\n", cases[i].group, cases[i].name);
        test_platform_uninstall();
        return 0;
    }

    FILE* out = options.report ? fopen(options.report, "w") : NULL;
    if (options.report && out == NULL) printf("warning: cannot write report %s\n", options.report);
    int failed = 0, skipped = 0, ran = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!matches(filter, cases[i].group, cases[i].name)) continue;
        test_begin_case(options.timeout_ms);
        clock_t start = clock();
        int rc = cases[i].fn();
        double ms = (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC;
        const char* status = "PASS";
        if (rc == TEST_SKIP || (rc == TEST_PASS && test_was_skipped())) status = "SKIP";
        else if (rc != TEST_PASS) status = "FAIL";
        ++ran;
        if (strcmp(status, "FAIL") == 0) ++failed;
        else if (strcmp(status, "SKIP") == 0) ++skipped;
        printf("[%s] %-34s %s (%.1f ms)\n", cases[i].group, cases[i].name, status, ms);
        if (out) fprintf(out, "%s,%s,%s,%.3f,%d\n", cases[i].group, cases[i].name, status, ms, rc);
        fflush(stdout);
    }
    if (out) fclose(out);
    test_platform_uninstall();
    printf("summary: %d ran, %d failed, %d skipped\n", ran, failed, skipped);
    return failed == 0 ? 0 : 1;
}
