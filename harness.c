#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "implementations.h"

// --- Cross-platform high-resolution timer ---
#if defined(__APPLE__)
#include <mach/mach_time.h>
static double ns_per_tick;
static void timer_init(void) {
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    ns_per_tick = (double)tb.numer / (double)tb.denom;
}
static uint64_t timer_now(void) { return mach_absolute_time(); }
static double timer_ns(uint64_t start, uint64_t end) {
    return (double)(end - start) * ns_per_tick;
}
#elif defined(_WIN32)
#include <windows.h>
static double ns_per_tick;
static void timer_init(void) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    ns_per_tick = 1e9 / (double)freq.QuadPart;
}
static uint64_t timer_now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)t.QuadPart;
}
static double timer_ns(uint64_t start, uint64_t end) {
    return (double)(end - start) * ns_per_tick;
}
#else
#include <time.h>
static void timer_init(void) {}
static uint64_t timer_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static double timer_ns(uint64_t start, uint64_t end) {
    return (double)(end - start);
}
#endif

// --- Configuration ---
#define WARMUP_ITERS  100000
#define BENCH_ITERS   10000000
#define NUM_GUIDS     256
#define NUM_SAMPLES   11

// --- Implementation registry ---
static impl_entry impls[] = {
    { "sprintf       ", guid_sprintf },
    { "dave_original ", guid_dave_original },
    { "lookup16      ", guid_lookup16 },
    { "unrolled      ", guid_unrolled },
    { "arithmetic    ", guid_arithmetic },
    { "swar          ", guid_swar },

#if defined(HAS_NEON) || defined(__aarch64__) || defined(_M_ARM64)
    { "neon_simd     ", guid_neon_simd },
    { "neon_tbl2     ", guid_neon_tbl2 },
    { "neon_scatter  ", guid_neon_scatter },
    { "neon_arith    ", guid_neon_arith },
    { "neon_ultimate ", guid_neon_ultimate },
    { "neon_fused    ", guid_neon_fused },
#endif

#if defined(HAS_SSE) || defined(__x86_64__) || defined(_M_X64)
    { "ssse3_scatter ", guid_ssse3_scatter },
    { "sse2_basic    ", guid_sse2_basic },
#endif

    { NULL, NULL }
};

static IID test_guids[NUM_GUIDS];

static void init_test_guids(void)
{
    srand(42);
    for (int i = 0; i < NUM_GUIDS; i++) {
        uint8_t *p = (uint8_t *)&test_guids[i];
        for (int j = 0; j < 16; j++)
            p[j] = rand() & 0xFF;
    }
}

static int correctness_check(void)
{
    IID known = {
        .Data1 = 0x01020304,
        .Data2 = 0x0506,
        .Data3 = 0x0708,
        .Data4 = { 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10 }
    };
    const char *expected = "01020304-0506-0708-090a-0b0c0d0e0f10";

    char buf[64];
    int pass = 1;

    for (int i = 0; impls[i].name; i++) {
        memset(buf, 'X', sizeof(buf));
        impls[i].fn(&known, buf);
        if (strcmp(buf, expected) != 0) {
            printf("FAIL %s: got \"%s\"\n       expected \"%s\"\n",
                   impls[i].name, buf, expected);
            pass = 0;
        }
    }

    IID zeros;
    memset(&zeros, 0, sizeof(zeros));
    const char *zero_expected = "00000000-0000-0000-0000-000000000000";
    for (int i = 0; impls[i].name; i++) {
        memset(buf, 'X', sizeof(buf));
        impls[i].fn(&zeros, buf);
        if (strcmp(buf, zero_expected) != 0) {
            printf("FAIL %s (zeros): got \"%s\"\n", impls[i].name, buf);
            pass = 0;
        }
    }

    IID ones;
    memset(&ones, 0xFF, sizeof(ones));
    const char *ones_expected = "ffffffff-ffff-ffff-ffff-ffffffffffff";
    for (int i = 0; impls[i].name; i++) {
        memset(buf, 'X', sizeof(buf));
        impls[i].fn(&ones, buf);
        if (strcmp(buf, ones_expected) != 0) {
            printf("FAIL %s (ones): got \"%s\"\n", impls[i].name, buf);
            pass = 0;
        }
    }

    // Cross-validate: all implementations must agree
    char ref[64], test[64];
    for (int g = 0; g < NUM_GUIDS; g++) {
        impls[0].fn(&test_guids[g], ref);
        for (int i = 1; impls[i].name; i++) {
            impls[i].fn(&test_guids[g], test);
            if (strcmp(ref, test) != 0) {
                printf("MISMATCH guid[%d] %s vs %s:\n  ref:  %s\n  test: %s\n",
                       g, impls[0].name, impls[i].name, ref, test);
                pass = 0;
            }
        }
    }

    return pass;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void benchmark(void)
{
    char buf[64];

    printf("\n%-16s %10s %10s %10s %10s %12s\n",
           "IMPLEMENTATION", "MIN(ns)", "MEDIAN(ns)", "MEAN(ns)", "P99(ns)", "ops/sec");
    printf("%-16s %10s %10s %10s %10s %12s\n",
           "----------------", "--------", "----------", "--------", "------", "----------");

    int num_impls = 0;
    while (impls[num_impls].name) num_impls++;
    double *medians = (double *)calloc((size_t)num_impls, sizeof(double));

    for (int impl = 0; impl < num_impls; impl++) {
        guid_fn fn = impls[impl].fn;

        for (int i = 0; i < WARMUP_ITERS; i++)
            fn(&test_guids[i & (NUM_GUIDS-1)], buf);

        double samples[NUM_SAMPLES];
        for (int s = 0; s < NUM_SAMPLES; s++) {
            uint64_t start = timer_now();
            for (int i = 0; i < BENCH_ITERS; i++) {
                fn(&test_guids[i & (NUM_GUIDS-1)], buf);
#if defined(__GNUC__) || defined(__clang__)
                __asm__ volatile("" : : "r"(buf) : "memory");
#endif
            }
            uint64_t end = timer_now();
            samples[s] = timer_ns(start, end) / BENCH_ITERS;
        }

        qsort(samples, NUM_SAMPLES, sizeof(double), cmp_double);

        double min_ns = samples[0];
        double median_ns = samples[NUM_SAMPLES / 2];
        double p99_ns = samples[(int)(NUM_SAMPLES * 0.99)];
        double mean_ns = 0;
        for (int s = 0; s < NUM_SAMPLES; s++) mean_ns += samples[s];
        mean_ns /= NUM_SAMPLES;

        medians[impl] = median_ns;

        printf("%-16s %10.2f %10.2f %10.2f %10.2f %12.0f\n",
               impls[impl].name, min_ns, median_ns, mean_ns, p99_ns, 1e9 / median_ns);
    }

    // Speedup tables (using stored medians for consistency)
    printf("\n--- SPEEDUP vs sprintf ---\n");
    for (int impl = 1; impl < num_impls; impl++)
        printf("  %s: %.1fx faster\n", impls[impl].name, medians[0] / medians[impl]);

    printf("\n--- SPEEDUP vs Dave's original ---\n");
    for (int impl = 2; impl < num_impls; impl++)
        printf("  %s: %.2fx faster\n", impls[impl].name, medians[1] / medians[impl]);

    free(medians);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    timer_init();
    init_test_guids();

    const char *arch =
#if defined(__aarch64__) || defined(_M_ARM64)
        "ARM64 (NEON)";
#elif defined(__x86_64__) || defined(_M_X64)
        "x86_64 (SSE2/SSSE3)";
#else
        "unknown (scalar only)";
#endif

    printf("=== Dave's Garage GUID-to-String Challenge ===\n");
    printf("Platform: %s\n", arch);
    printf("Iterations: %d per sample, %d samples (median reported)\n",
           BENCH_ITERS, NUM_SAMPLES);
    printf("GUID pool: %d random GUIDs (warm cache)\n\n", NUM_GUIDS);

    printf("--- Correctness Verification ---\n");
    if (!correctness_check()) {
        printf("\nCORRECTNESS FAILURES detected. Fix before benchmarking.\n");
        return 1;
    }
    printf("All %d implementations produce identical, correct output.\n",
           (int)(sizeof(impls)/sizeof(impls[0])) - 1);

    benchmark();

    printf("\nDone.\n");
    return 0;
}
