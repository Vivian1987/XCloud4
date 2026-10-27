/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/auth/release_check.h"
#include "../src/auth/http_client.h"
#include "../src/ui/screen.h"
#include "../src/ui/catalog_gui.h"
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

    /* 1. Catalog screen with XErr 2148916233 */
    X4Screen screen_cat = {.page = 5, .selected = 0};
    X4CatalogBrowser cat_browser = {0};
    X4CatalogSnapshot cat_snap = {
        .state = X4_CATALOG_ERROR,
        .error = 0,
        .http_status = 401,
        .xerr = 2148916233u,
        .count = 0,
        .total = 0
    };
    X4CatalogGuiStatus gui_status = {
        .connected = 1,
        .busy = 0,
        .pending = 0,
        .notice = NULL
    };

    /* Spanish Catalog */
    x4_lang_set(X4_LANG_ES, "es-ES", "ES");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_cat, &ctrl, fb);
    x4_catalog_gui_draw(&cat_snap, &cat_browser, NULL, NULL, &gui_status, fb);
    snprintf(path, sizeof(path), "%s/catalogo_xerr_es.ppm", out_dir);
    write_ppm(path, fb);

    /* English Catalog */
    x4_lang_set(X4_LANG_EN, "en-US", "US");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_cat, &ctrl, fb);
    x4_catalog_gui_draw(&cat_snap, &cat_browser, NULL, NULL, &gui_status, fb);
    snprintf(path, sizeof(path), "%s/catalogo_xerr_en.ppm", out_dir);
    write_ppm(path, fb);

    /* 2. Login screen with 0x804101E1 and its DNS hint */
    X4Screen screen_account = {.page = 4, .selected = 0};
    X4AuthSnapshot auth_snap = {
        .state = X4_AUTH_ERROR,
        .error = (int)0x804101E1u,
        .http_status = 0,
        .stage = "DNS",
        .seconds_left = 0
    };

    /* Spanish Auth */
    x4_lang_set(X4_LANG_ES, "es-ES", "ES");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_account, &ctrl, fb);
    x4_auth_draw(&auth_snap, 0, 0, fb);
    snprintf(path, sizeof(path), "%s/auth_dns_es.ppm", out_dir);
    write_ppm(path, fb);

    /* English Auth */
    x4_lang_set(X4_LANG_EN, "en-US", "US");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_account, &ctrl, fb);
    x4_auth_draw(&auth_snap, 0, 0, fb);
    snprintf(path, sizeof(path), "%s/auth_dns_en.ppm", out_dir);
    write_ppm(path, fb);

    /* 3. About page with new requirement text */
    X4Screen screen_about = {.page = 2, .selected = 0};
    x4_release_check_set_mock(X4_UPDATE_LATEST, NULL);

    /* Spanish About */
    x4_lang_set(X4_LANG_ES, "es-ES", "ES");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_about, &ctrl, fb);
    snprintf(path, sizeof(path), "%s/acerca_requisitos_es.ppm", out_dir);
    write_ppm(path, fb);

    /* English About */
    x4_lang_set(X4_LANG_EN, "en-US", "US");
    memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
    x4_screen_draw(&screen_about, &ctrl, fb);
    snprintf(path, sizeof(path), "%s/acerca_requisitos_en.ppm", out_dir);
    write_ppm(path, fb);

    free(fb);
    x4_font_close();
    printf("PASS capture_tarea11_screens\n");
    return 0;
}
