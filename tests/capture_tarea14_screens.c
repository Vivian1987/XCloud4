/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/auth/release_check.h"
#include "../src/auth/http_client.h"
#include "../src/ui/screen.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/picture_settings.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host stubs for http client in tests */
X4Http *x4_http_open(int *err) { if (err) *err = 0; return NULL; }
void x4_http_close(X4Http *h) { (void)h; }
int x4_http_releases_request(X4Http *h, const char *url, char *body, size_t cap,
    size_t *len, int *status, const _Atomic int *cancel)
{
    (void)h; (void)url; (void)body; (void)cap; (void)len; (void)status; (void)cancel;
    return -1;
}

/* Stubs for catalog assets in tests */
struct X4CatalogAssets { int unused; };
int x4_catalog_assets_acquire(X4CatalogAssets *a, const char *id, X4CatalogAssetView *out)
{
    (void)a; (void)id; (void)out; return 0;
}
void x4_catalog_assets_release(X4CatalogAssets *a, X4CatalogAssetView *v)
{
    (void)a; (void)v;
}
bool x4_server_settings_awaiting_first_choice(void) { return false; }

static void write_ppm(const char *path, const uint32_t *p)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n1920 1080\n255\n");
    for (size_t i = 0; i < 1920 * 1080; ++i) {
        uint8_t rgb[3] = {
            (uint8_t)(p[i] & 0xff),
            (uint8_t)((p[i] >> 8) & 0xff),
            (uint8_t)((p[i] >> 16) & 0xff)
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *out_dir = argc > 1 ? argv[1] : ".";
    const char *font_path = argc > 2 ? argv[2] : "assets/fonts/noto_sans.x4font";

    int font_rc = x4_font_init(font_path);
    assert(font_rc == 0);

    uint32_t *fb = calloc(1920 * 1080, sizeof(uint32_t));
    assert(fb);

    X4Controller ctrl = {0};
    ctrl.data.connected = 1;
    char path[512];

    X4Screen screen_home = {.page = 0, .selected = 0};
    X4PictureSettingsUi ui = {.open = true, .selected = 0};

    enum X4Lang langs[2] = {X4_LANG_ES, X4_LANG_EN};
    const char *lang_suffixes[2] = {"es", "en"};
    const char *locales[2] = {"es-ES", "en-US"};
    const char *markets[2] = {"ES", "US"};

    for (int l = 0; l < 2; ++l) {
        x4_lang_set(langs[l], locales[l], markets[l]);
        const char *sfx = lang_suffixes[l];

        /* 1. Home screen footer with OPTIONS hint */
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        snprintf(path, sizeof(path), "%s/pie_options_inicio_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 2. Modal with Option 0: 1080p Alta (recomendada) */
        ui.selected = 0;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/menu_calidad_1080p_alta_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 3. Modal with Option 1: 1080p */
        ui.selected = 1;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/menu_calidad_1080p_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 4. Modal with Option 2: 720p */
        ui.selected = 2;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/menu_calidad_720p_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);
    }

    free(fb);
    x4_font_close();
    printf("PASS capture_tarea14_screens\n");
    return 0;
}
