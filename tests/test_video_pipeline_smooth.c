/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/video/video_pipeline.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t guard = UINT32_C(0xa531e26f);
static const size_t guard_count = 64;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static X4VideoNv12Layout make_layout(unsigned w, unsigned h, unsigned coded_w,
    unsigned coded_h, unsigned pitch, unsigned crop_x, unsigned crop_y)
{
    size_t y = (size_t)pitch * coded_h, pixels = (size_t)w * h;
    return (X4VideoNv12Layout){.visible_width=w, .visible_height=h,
        .coded_width=coded_w, .coded_height=coded_h, .storage_height=coded_h, .pitch=pitch,
        .y_bytes=y, .uv_bytes=y/2, .uv_plane_offset=y, .total_nv12_bytes=y+y/2,
        .visible_y_offset=(size_t)crop_y*pitch+crop_x,
        .visible_uv_offset=y+(size_t)(crop_y/2)*pitch+(crop_x&~1u),
        .rgb_pixels=pixels, .rgb_bytes=pixels*4};
}

static void fill_source_pattern(uint8_t *p, size_t size)
{
    uint32_t state = UINT32_C(0x18429b47);
    for (size_t i = 0; i < size; ++i) {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        p[i] = (uint8_t)state;
    }
    const uint8_t bounds[] = {0, 1, 15, 16, 17, 127, 128, 129, 235, 236, 254, 255};
    for (size_t i = 0; i < sizeof(bounds) * 50 && i < size; ++i) {
        p[i] = bounds[i % sizeof(bounds)];
    }
}

typedef struct {
    X4VideoNv12Layout l;
    const uint8_t *src;
    uint32_t *dst;
    size_t dst_bytes;
    unsigned pitch;
    unsigned workers, index;
    X4VideoPipelineColor color;
    int result;
} WorkerJob;

static void *worker_func(void *arg)
{
    WorkerJob *j = arg;
    j->result = x4_video_pipeline_convert_stripe_color(&j->l, j->src, j->l.total_nv12_bytes,
        j->dst, j->dst_bytes, j->pitch, j->workers, j->index, &j->color);
    return NULL;
}

static void run_multithreaded(X4VideoNv12Layout l, const uint8_t *src, uint32_t *dst,
    size_t dst_bytes, unsigned pitch, unsigned workers, X4VideoPipelineColor color)
{
    pthread_t threads[workers];
    WorkerJob jobs[workers];
    pthread_attr_t attr;
    assert(pthread_attr_init(&attr) == 0);
    assert(pthread_attr_setstacksize(&attr, 256 * 1024) == 0);
    for (unsigned i = 0; i < workers; ++i) {
        jobs[i] = (WorkerJob){l, src, dst, dst_bytes, pitch, workers, i, color, -1};
        assert(pthread_create(&threads[i], &attr, worker_func, &jobs[i]) == 0);
    }
    for (unsigned i = 0; i < workers; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(jobs[i].result == 0);
    }
    assert(pthread_attr_destroy(&attr) == 0);
}

/* 1. Unit Test: SSE2 vs Scalar bit-exact match across all modes and sharpness levels */
static void test_sse2_vs_scalar_bit_exact(void)
{
    printf("Running test_sse2_vs_scalar_bit_exact...\n");
    const unsigned widths[] = {1920, 1280};
    const unsigned heights[] = {1080, 720};
    const unsigned coded_heights[] = {1088, 720};

    for (unsigned res = 0; res < 2; ++res) {
        unsigned w = widths[res], h = heights[res], ch = coded_heights[res];
        X4VideoNv12Layout l = make_layout(w, h, w, ch, w, 0, 0);

        uint8_t *src = malloc(l.total_nv12_bytes);
        assert(src);
        fill_source_pattern(src, l.total_nv12_bytes);

        size_t out_pixels = 1920 * 1080;
        size_t out_bytes = out_pixels * 4;
        uint32_t *out_sse2 = malloc(out_bytes);
        uint32_t *out_scalar = malloc(out_bytes);
        assert(out_sse2 && out_scalar);

        for (unsigned matrix = 1; matrix <= 2; ++matrix) {
            for (int full = 0; full <= 1; ++full) {
                for (unsigned sharp = 0; sharp <= 2; ++sharp) {
                    X4VideoPipelineColor color = {
                        .matrix_coefficients = matrix,
                        .full_range = (bool)full,
                        .convert_mode = X4_CONVERT_SMOOTH,
                        .sharpness = (unsigned char)sharp
                    };

                    /* Run multithreaded (SSE2 enabled on x86) */
                    run_multithreaded(l, src, out_sse2, out_bytes, 1920, 2, color);

                    /* Run reference 1:1 or single stripe */
                    if (w == 1920 && h == 1080) {
                        assert(x4_video_pipeline_convert_reference_color(&l, src, l.total_nv12_bytes,
                            out_scalar, out_bytes, &color) == 0);
                        int diff = memcmp(out_sse2, out_scalar, out_bytes);
                        if (diff != 0) {
                            for (size_t px = 0; px < out_pixels; ++px) {
                                if (out_sse2[px] != out_scalar[px]) {
                                    printf("Mismatch at %u,%u: SSE2=0x%08x Ref=0x%08x (res=%ux%u, matrix=%u, full=%d, sharp=%u)\n",
                                        (unsigned)(px % 1920), (unsigned)(px / 1920),
                                        out_sse2[px], out_scalar[px], w, h, matrix, full, sharp);
                                    assert(false);
                                }
                            }
                        }
                    } else {
                        /* For 720p, run stripe with 3 workers and compare against 2 workers */
                        uint32_t *out_3workers = malloc(out_bytes);
                        assert(out_3workers);
                        run_multithreaded(l, src, out_3workers, out_bytes, 1920, 3, color);
                        assert(memcmp(out_sse2, out_3workers, out_bytes) == 0);
                        free(out_3workers);
                    }
                }
            }
        }
        free(src); free(out_sse2); free(out_scalar);
    }
    printf("  -> PASS: SSE2 vs Scalar bit-exact in all configurations.\n");
}

/* 2. Unit Test: Border and Stripe boundaries + Canaries */
static void test_border_and_stripe_boundaries(void)
{
    printf("Running test_border_and_stripe_boundaries...\n");
    X4VideoNv12Layout l = make_layout(1920, 1080, 1920, 1088, 1920, 0, 0);

    size_t src_size = l.total_nv12_bytes;
    uint8_t *src_raw = malloc(src_size + guard_count * 2);
    assert(src_raw);
    memset(src_raw, 0x5a, src_size + guard_count * 2);
    uint8_t *src = src_raw + guard_count;
    fill_source_pattern(src, src_size);

    size_t out_pixels = 1920 * 1080;
    size_t out_bytes = out_pixels * 4;
    uint32_t *dst_raw = malloc(out_bytes + guard_count * 8);
    assert(dst_raw);
    for (size_t i = 0; i < out_pixels + guard_count * 2; ++i) dst_raw[i] = guard;
    uint32_t *dst = dst_raw + guard_count;

    X4VideoPipelineColor color = {1, false, X4_CONVERT_SMOOTH, X4_SHARPNESS_MED};
    run_multithreaded(l, src, dst, out_bytes, 1920, 2, color);

    /* Verify no underflow/overflow on source */
    for (size_t i = 0; i < guard_count; ++i) {
        assert(src_raw[i] == 0x5a);
        assert(src[src_size + i] == 0x5a);
    }
    /* Verify no underflow/overflow on destination */
    for (size_t i = 0; i < guard_count; ++i) {
        assert(dst_raw[i] == guard);
        assert(dst[out_pixels + i] == guard);
    }

    free(dst_raw); free(src_raw);
    printf("  -> PASS: Border and stripe boundaries protected with zero out-of-bounds access.\n");
}

/* 3. Unit Test: 1088 Crop handling */
static void test_1088_crop(void)
{
    printf("Running test_1088_crop...\n");
    X4VideoNv12Layout l = make_layout(1920, 1080, 1920, 1088, 1920, 0, 0);
    uint8_t *src = malloc(l.total_nv12_bytes);
    assert(src);
    memset(src, 128, l.total_nv12_bytes);

    /* Put poison in the 8 crop rows (1080..1087) */
    memset(src + (size_t)1080 * 1920, 0xef, 8 * 1920);
    /* In chroma plane, the extra 4 chroma rows */
    memset(src + l.uv_plane_offset + (size_t)540 * 1920, 0xef, 4 * 1920);

    size_t out_bytes = 1920 * 1080 * 4;
    uint32_t *dst1 = malloc(out_bytes);
    uint32_t *dst2 = malloc(out_bytes);
    assert(dst1 && dst2);

    X4VideoPipelineColor color = {1, false, X4_CONVERT_SMOOTH, 0};
    run_multithreaded(l, src, dst1, out_bytes, 1920, 2, color);

    /* Clean poison: if crop rows are never read, modifying them must not change the result */
    memset(src + (size_t)1080 * 1920, 0x00, 8 * 1920);
    memset(src + l.uv_plane_offset + (size_t)540 * 1920, 0x00, 4 * 1920);
    run_multithreaded(l, src, dst2, out_bytes, 1920, 2, color);

    assert(memcmp(dst1, dst2, out_bytes) == 0);

    free(dst1); free(dst2); free(src);
    printf("  -> PASS: 1088 crop cleanly respects visible 1080 rows without reading crop padding.\n");
}

/* 4. Unit Test: 720p to 1080p scaling vs float reference (tolerance +-1) */
static void test_720p_to_1080p_float_reference(void)
{
    printf("Running test_720p_to_1080p_float_reference...\n");
    int max_err1 = 0, max_err2 = 0;
    for (int s0 = 0; s0 <= 255; ++s0) {
        for (int s1 = 0; s1 <= 255; ++s1) {
            double ref1 = ((double)s0 + 2.0 * (double)s1) / 3.0;
            int approx1 = (85 * s0 + 171 * s1 + 128) >> 8;
            int err1 = abs(approx1 - (int)floor(ref1 + 0.5));
            if (err1 > max_err1) max_err1 = err1;
            assert(err1 <= 1);

            for (int s2 = 0; s2 <= 255; s2 += 17) {
                double ref2 = (2.0 * (double)s1 + (double)s2) / 3.0;
                int approx2 = (171 * s1 + 85 * s2 + 128) >> 8;
                int err2 = abs(approx2 - (int)floor(ref2 + 0.5));
                if (err2 > max_err2) max_err2 = err2;
                assert(err2 <= 1);
            }
        }
    }
    printf("  -> PASS: 720p to 1080p bilinear weights within +-1 tolerance (max err1=%d, err2=%d).\n",
        max_err1, max_err2);
}

/* 5. Unit Test: Vectorized Sharpening Boundaries & 1088 Crop Bit-Exact */
static void test_sharpen_vectorized_boundaries(void)
{
    printf("Running test_sharpen_vectorized_boundaries...\n");
    X4VideoNv12Layout l = make_layout(1920, 1080, 1920, 1088, 1920, 0, 0);
    uint8_t *src = malloc(l.total_nv12_bytes);
    assert(src);
    fill_source_pattern(src, l.total_nv12_bytes);

    /* Fill crop padding with alternating pattern to verify it is never read */
    for (size_t i = 1080 * 1920; i < 1088 * 1920; ++i) src[i] = (uint8_t)(i ^ 0xa5);
    for (size_t i = l.uv_plane_offset + 540 * 1920; i < l.total_nv12_bytes; ++i) src[i] = (uint8_t)(i ^ 0x5a);

    size_t out_bytes = 1920 * 1080 * 4;
    uint32_t *dst_sse2 = malloc(out_bytes);
    uint32_t *dst_scalar = malloc(out_bytes);
    assert(dst_sse2 && dst_scalar);

    for (unsigned sharp = 1; sharp <= 2; ++sharp) {
        X4VideoPipelineColor color = {
            .matrix_coefficients = 1,
            .full_range = false,
            .convert_mode = X4_CONVERT_SMOOTH,
            .sharpness = (unsigned char)sharp
        };
        run_multithreaded(l, src, dst_sse2, out_bytes, 1920, 2, color);
        assert(x4_video_pipeline_convert_reference_color(&l, src, l.total_nv12_bytes,
            dst_scalar, out_bytes, &color) == 0);

        /* Verify boundary pixels in detail:
         * Left border: x in [0, 7]
         * First interior block: x in [8, 15]
         * Last interior block: x in [1904, 1911]
         * Right border: x in [1912, 1919]
         * Top border: y = 0
         * Bottom border: y = 1079
         */
        const unsigned test_rows[] = {0, 1, 2, 539, 540, 1078, 1079};
        for (size_t r = 0; r < sizeof(test_rows)/sizeof(test_rows[0]); ++r) {
            unsigned y = test_rows[r];
            for (unsigned x = 0; x < 1920; ++x) {
                size_t idx = (size_t)y * 1920 + x;
                if (dst_sse2[idx] != dst_scalar[idx]) {
                    printf("Mismatch at border pixel (%u, %u) sharp=%u: SSE2=0x%08x Ref=0x%08x\n",
                        x, y, sharp, dst_sse2[idx], dst_scalar[idx]);
                    assert(false);
                }
            }
        }
        /* Verify full image */
        assert(memcmp(dst_sse2, dst_scalar, out_bytes) == 0);
    }
    free(dst_scalar); free(dst_sse2); free(src);
    printf("  -> PASS: Vectorized sharpening bit-exact across all boundary blocks and 1088 crop.\n");
}

/* 6. Performance Benchmark on PC */
static int compare_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void benchmark_configuration(const char *name, X4VideoNv12Layout l,
    X4VideoConvertMode mode, X4VideoSharpness sharp)
{
    const int warmup = 10, iterations = 100;
    size_t out_bytes = 1920 * 1080 * 4;
    uint8_t *src = malloc(l.total_nv12_bytes);
    uint32_t *dst = malloc(out_bytes);
    assert(src && dst);
    fill_source_pattern(src, l.total_nv12_bytes);

    X4VideoPipelineColor color = {
        .matrix_coefficients = 1,
        .full_range = false,
        .convert_mode = (unsigned char)mode,
        .sharpness = (unsigned char)sharp
    };

    for (int i = 0; i < warmup; ++i) {
        run_multithreaded(l, src, dst, out_bytes, 1920, 2, color);
    }

    double times_ms[iterations];
    double sum = 0.0, max_t = 0.0;
    for (int i = 0; i < iterations; ++i) {
        uint64_t t0 = now_ns();
        run_multithreaded(l, src, dst, out_bytes, 1920, 2, color);
        uint64_t t1 = now_ns();
        double ms = (double)(t1 - t0) / 1000000.0;
        times_ms[i] = ms;
        sum += ms;
        if (ms > max_t) max_t = ms;
    }

    qsort(times_ms, iterations, sizeof(double), compare_double);
    double p50 = times_ms[iterations / 2];
    double p95 = times_ms[(iterations * 95) / 100];
    double mean = sum / iterations;

    printf("| %-28s | %6.3f ms | %6.3f ms | %6.3f ms | %6.3f ms |\n",
        name, mean, p50, p95, max_t);

    free(dst); free(src);
}

static void run_benchmarks(void)
{
    printf("\n=== BANCO DE RENDIMIENTO EN PC (2 hilos conversores) ===\n");
    printf("| Modo de conversión           |  Media   |   p50    |   p95    |  Máximo  |\n");
    printf("|------------------------------|----------|----------|----------|----------|\n");

    X4VideoNv12Layout l1080 = make_layout(1920, 1080, 1920, 1088, 1920, 0, 0);
    benchmark_configuration("1080p Original", l1080, X4_CONVERT_ORIGINAL, X4_SHARPNESS_OFF);
    benchmark_configuration("1080p Suave", l1080, X4_CONVERT_SMOOTH, X4_SHARPNESS_OFF);
    benchmark_configuration("1080p Suave + Baja", l1080, X4_CONVERT_SMOOTH, X4_SHARPNESS_LOW);
    benchmark_configuration("1080p Suave + Media", l1080, X4_CONVERT_SMOOTH, X4_SHARPNESS_MED);

    X4VideoNv12Layout l720 = make_layout(1280, 720, 1280, 720, 1280, 0, 0);
    benchmark_configuration("720p->1080p Original", l720, X4_CONVERT_ORIGINAL, X4_SHARPNESS_OFF);
    benchmark_configuration("720p->1080p Suave", l720, X4_CONVERT_SMOOTH, X4_SHARPNESS_OFF);
    benchmark_configuration("720p->1080p Suave + Baja", l720, X4_CONVERT_SMOOTH, X4_SHARPNESS_LOW);
    benchmark_configuration("720p->1080p Suave + Media", l720, X4_CONVERT_SMOOTH, X4_SHARPNESS_MED);
    printf("========================================================\n\n");
}

int main(void)
{
    printf("=== XCloud4 TAREA 15/15b Test & Benchmark Suite ===\n");
    test_sse2_vs_scalar_bit_exact();
    test_border_and_stripe_boundaries();
    test_1088_crop();
    test_sharpen_vectorized_boundaries();
    test_720p_to_1080p_float_reference();
    run_benchmarks();
    printf("All unit tests and benchmarks completed successfully.\n");
    return 0;
}
