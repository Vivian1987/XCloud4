/* SPDX-License-Identifier: GPL-3.0-only */
#if defined(__has_include)
#if __has_include("../src/video/video_pipeline.h")
#include "../src/video/video_pipeline.h"
#else
#include "video_pipeline.h"
#endif
#else
#include "video_pipeline.h"
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_bmp(const char *filename, const uint32_t *pixels, unsigned w, unsigned h, unsigned pitch)
{
    FILE *f = fopen(filename, "wb");
    if (!f) { perror(filename); return; }
    unsigned row_bytes = (w * 3 + 3) & ~3u;
    unsigned data_size = row_bytes * h;
    unsigned file_size = 54 + data_size;
    int32_t neg_h = -(int32_t)h;
    uint8_t header[54] = {
        'B', 'M',
        file_size & 0xff, (file_size >> 8) & 0xff, (file_size >> 16) & 0xff, (file_size >> 24) & 0xff,
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        w & 0xff, (w >> 8) & 0xff, (w >> 16) & 0xff, (w >> 24) & 0xff,
        neg_h & 0xff, (neg_h >> 8) & 0xff, (neg_h >> 16) & 0xff, (neg_h >> 24) & 0xff,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        data_size & 0xff, (data_size >> 8) & 0xff, (data_size >> 16) & 0xff, (data_size >> 24) & 0xff,
        0x13, 0x0b, 0, 0,
        0x13, 0x0b, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);
    uint8_t *row_buf = malloc(row_bytes);
    for (unsigned y = 0; y < h; ++y) {
        const uint32_t *src_row = pixels + (size_t)y * pitch;
        for (unsigned x = 0; x < w; ++x) {
            uint32_t p = src_row[x];
            row_buf[x * 3 + 0] = (p >> 16) & 0xff;
            row_buf[x * 3 + 1] = (p >> 8) & 0xff;
            row_buf[x * 3 + 2] = p & 0xff;
        }
        for (unsigned pad = w * 3; pad < row_bytes; ++pad) row_buf[pad] = 0;
        fwrite(row_buf, 1, row_bytes, f);
    }
    free(row_buf);
    fclose(f);
}

static void write_bmp_zoom3x(const char *filename, const uint32_t *pixels, unsigned src_pitch,
    unsigned x0, unsigned y0, unsigned crop_w, unsigned crop_h)
{
    unsigned out_w = crop_w * 3, out_h = crop_h * 3;
    FILE *f = fopen(filename, "wb");
    if (!f) { perror(filename); return; }
    unsigned row_bytes = (out_w * 3 + 3) & ~3u;
    unsigned data_size = row_bytes * out_h;
    unsigned file_size = 54 + data_size;
    int32_t neg_h = -(int32_t)out_h;
    uint8_t header[54] = {
        'B', 'M',
        file_size & 0xff, (file_size >> 8) & 0xff, (file_size >> 16) & 0xff, (file_size >> 24) & 0xff,
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        out_w & 0xff, (out_w >> 8) & 0xff, (out_w >> 16) & 0xff, (out_w >> 24) & 0xff,
        neg_h & 0xff, (neg_h >> 8) & 0xff, (neg_h >> 16) & 0xff, (neg_h >> 24) & 0xff,
        1, 0,
        24, 0,
        0, 0, 0, 0,
        data_size & 0xff, (data_size >> 8) & 0xff, (data_size >> 16) & 0xff, (data_size >> 24) & 0xff,
        0x13, 0x0b, 0, 0,
        0x13, 0x0b, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);
    uint8_t *row_buf = malloc(row_bytes);
    for (unsigned y = 0; y < crop_h; ++y) {
        const uint32_t *src_row = pixels + (size_t)(y0 + y) * src_pitch + x0;
        for (unsigned x = 0; x < crop_w; ++x) {
            uint32_t p = src_row[x];
            uint8_t b = (p >> 16) & 0xff, g = (p >> 8) & 0xff, r = p & 0xff;
            for (int k = 0; k < 3; ++k) {
                row_buf[(x * 3 + k) * 3 + 0] = b;
                row_buf[(x * 3 + k) * 3 + 1] = g;
                row_buf[(x * 3 + k) * 3 + 2] = r;
            }
        }
        for (unsigned pad = out_w * 3; pad < row_bytes; ++pad) row_buf[pad] = 0;
        for (int rep = 0; rep < 3; ++rep) fwrite(row_buf, 1, row_bytes, f);
    }
    free(row_buf);
    fclose(f);
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
} Job;

static void *thread_worker(void *arg)
{
    Job *j = arg;
    j->result = x4_video_pipeline_convert_stripe_color(&j->l, j->src, j->l.total_nv12_bytes,
        j->dst, j->dst_bytes, j->pitch, j->workers, j->index, &j->color);
    return NULL;
}

static void convert_frame(X4VideoNv12Layout l, const uint8_t *src, uint32_t *dst,
    size_t dst_bytes, unsigned pitch, X4VideoPipelineColor color)
{
    pthread_t threads[2];
    Job jobs[2];
    pthread_attr_t attr;
    assert(pthread_attr_init(&attr) == 0);
    assert(pthread_attr_setstacksize(&attr, 256 * 1024) == 0);
    for (unsigned i = 0; i < 2; ++i) {
        jobs[i] = (Job){l, src, dst, dst_bytes, pitch, 2, i, color, -1};
        assert(pthread_create(&threads[i], &attr, thread_worker, &jobs[i]) == 0);
    }
    for (unsigned i = 0; i < 2; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(jobs[i].result == 0);
    }
    assert(pthread_attr_destroy(&attr) == 0);
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

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input.nv12> <out_prefix> <crop_type: hud|synthetic_text|synthetic_lines>\n", argv[0]);
        return 1;
    }
    const char *input_path = argv[1];
    const char *out_prefix = argv[2];
    const char *crop_type = argv[3];

    FILE *f = fopen(input_path, "rb");
    if (!f) { perror(input_path); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *src = malloc(size);
    assert(src && fread(src, 1, size, f) == (size_t)size);
    fclose(f);

    X4VideoNv12Layout l = make_layout(1280, 720, 1280, 720, 1280, 0, 0);
    size_t out_bytes = (size_t)1920 * 1080 * sizeof(uint32_t);
    uint32_t *dst = malloc(out_bytes);
    assert(dst);

    struct {
        const char *name;
        unsigned char mode;
        unsigned char sharp;
    } modes[] = {
        {"original", X4_CONVERT_ORIGINAL, X4_SHARPNESS_OFF},
        {"suave", X4_CONVERT_SMOOTH, X4_SHARPNESS_OFF},
        {"suave_baja", X4_CONVERT_SMOOTH, X4_SHARPNESS_LOW},
        {"suave_media", X4_CONVERT_SMOOTH, X4_SHARPNESS_MED}
    };

    unsigned crop_x0 = 1460, crop_y0 = 890, crop_w = 340, crop_h = 150;
    if (strcmp(crop_type, "synthetic_text") == 0) {
        crop_x0 = 220; crop_y0 = 590; crop_w = 360; crop_h = 150;
    } else if (strcmp(crop_type, "synthetic_lines") == 0) {
        crop_x0 = 220; crop_y0 = 220; crop_w = 320; crop_h = 180;
    }

    for (unsigned m = 0; m < 4; ++m) {
        X4VideoPipelineColor color = {
            .matrix_coefficients = 1, /* BT.709 */
            .full_range = false,
            .convert_mode = modes[m].mode,
            .sharpness = modes[m].sharp
        };
        convert_frame(l, src, dst, out_bytes, 1920, color);

        char out_full[512], out_zoom[512];
        snprintf(out_full, sizeof(out_full), "%s_%s.bmp", out_prefix, modes[m].name);
        snprintf(out_zoom, sizeof(out_zoom), "%s_%s_zoom3x.bmp", out_prefix, modes[m].name);

        write_bmp(out_full, dst, 1920, 1080, 1920);
        write_bmp_zoom3x(out_zoom, dst, 1920, crop_x0, crop_y0, crop_w, crop_h);
        printf("Written: %s and %s\n", out_full, out_zoom);
    }

    free(dst);
    free(src);
    return 0;
}
