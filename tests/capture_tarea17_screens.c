/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/auth/release_check.h"
#include "../src/auth/http_client.h"
#include "../src/ui/screen.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/picture_settings.h"
#include "../src/ui/server_settings.h"
#include "../src/ui/stream_settings.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"
#include "../src/auth/device_auth.h"
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

    enum X4Lang langs[2] = {X4_LANG_ES, X4_LANG_EN};
    const char *lang_suffixes[2] = {"es", "en"};
    const char *locales[2] = {"es-ES", "en-US"};
    const char *markets[2] = {"ES", "US"};

    /* Sample mock regions */
    X4ServerRegion sample_regions[3];
    memset(sample_regions, 0, sizeof(sample_regions));
    snprintf(sample_regions[0].name, sizeof(sample_regions[0].name), "MexicoCentral");
    snprintf(sample_regions[0].host, sizeof(sample_regions[0].host), "mexicocentral.gssv-play-prod.xboxlive.com");
    sample_regions[0].is_default = true;
    sample_regions[0].last_game_ms = 21;

    snprintf(sample_regions[1].name, sizeof(sample_regions[1].name), "BrazilSouth");
    snprintf(sample_regions[1].host, sizeof(sample_regions[1].host), "brazilsouth.gssv-play-prod.xboxlive.com");
    sample_regions[1].is_default = false;
    sample_regions[1].last_game_ms = 172;

    snprintf(sample_regions[2].name, sizeof(sample_regions[2].name), "WestEurope");
    snprintf(sample_regions[2].host, sizeof(sample_regions[2].host), "westeurope.gssv-play-prod.xboxlive.com");
    sample_regions[2].is_default = false;
    sample_regions[2].last_game_ms = 0; /* unmeasured */

    x4_server_settings_set_path("scratch_srv17.bin");
    remove("scratch_srv17.bin");
    x4_server_settings_init();
    x4_server_settings_update_regions(sample_regions, 3);
    x4_server_settings_set_selected("MexicoCentral", true);

    X4CatalogSnapshot ready_cat = {0};
    ready_cat.state = X4_CATALOG_READY;
    ready_cat.count = 1;
    snprintf(ready_cat.titles[0].name, sizeof(ready_cat.titles[0].name), "Fortnite");
    snprintf(ready_cat.titles[0].id, sizeof(ready_cat.titles[0].id), "BT5P2X999VH2");

    X4CatalogSnapshot err_cat = {0};
    err_cat.state = X4_CATALOG_ERROR;
    err_cat.error = X4_AUTH_E_FORTNITE_NOT_FOUND;

    for (int l = 0; l < 2; ++l) {
        x4_lang_set(langs[l], locales[l], markets[l]);
        const char *sfx = lang_suffixes[l];

        /* 1. Main menu with 4 cards (FORTNITE selected) */
        X4Screen s_home = {.page = 0, .selected = 1};
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_home, &ctrl, fb);
        snprintf(path, sizeof(path), "%s/main_menu_4_cards_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* Main menu with Account selected */
        s_home.selected = 0;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_home, &ctrl, fb);
        snprintf(path, sizeof(path), "%s/main_menu_4_cards_account_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 2. Fortnite summary screen with 3 qualities (with ms, MexicoCentral 21 ms) */
        x4_server_settings_set_selected("MexicoCentral", true);
        X4Screen s_sum = {.page = 8, .selected = 0};

        /* 1080p Alta (0) */
        s_sum.selected = 0;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_sum, &ctrl, fb);
        x4_fortnite_summary_draw(&s_sum, &ready_cat, 0, fb);
        snprintf(path, sizeof(path), "%s/fortnite_summary_1080p_alta_ms_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 1080p (1) */
        s_sum.selected = 1;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_sum, &ctrl, fb);
        x4_fortnite_summary_draw(&s_sum, &ready_cat, 0, fb);
        snprintf(path, sizeof(path), "%s/fortnite_summary_1080p_ms_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 720p (2) */
        s_sum.selected = 2;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_sum, &ctrl, fb);
        x4_fortnite_summary_draw(&s_sum, &ready_cat, 0, fb);
        snprintf(path, sizeof(path), "%s/fortnite_summary_720p_ms_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* Summary with "sin medir" / not measured (WestEurope) */
        x4_server_settings_set_selected("WestEurope", true);
        s_sum.selected = 0;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_sum, &ctrl, fb);
        x4_fortnite_summary_draw(&s_sum, &ready_cat, 0, fb);
        snprintf(path, sizeof(path), "%s/fortnite_summary_not_measured_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 3. Fortnite unavailable message */
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_sum, &ctrl, fb);
        x4_fortnite_summary_draw(&s_sum, &err_cat, 0, fb);
        snprintf(path, sizeof(path), "%s/fortnite_unavailable_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 4. Lab footers without OPTIONS SALIR */
        /* Account page (page 4) */
        X4Screen s_acc = {.page = 4, .selected = 0};
        X4AuthSnapshot auth_snap = {.state = X4_AUTH_IDLE};
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&s_acc, &ctrl, fb);
        x4_auth_draw(&auth_snap, 0, 0, fb);
        snprintf(path, sizeof(path), "%s/lab_footer_account_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* Catalog page (page 5) */
        X4CatalogBrowser cat_b = {0};
        X4RecentGames recent = {0};
        X4CatalogGuiStatus gui = {ctrl.data.connected, 0, 0, ""};
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_catalog_gui_draw(&ready_cat, &cat_b, &recent, NULL, &gui, fb);
        snprintf(path, sizeof(path), "%s/lab_footer_catalog_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* R3 panel */
#if !X4_PUBLIC
        X4StreamSettingsUi r3_ui = {0};
        r3_ui.open = true;
        x4_stream_settings_ui_draw(&r3_ui, fb);
        snprintf(path, sizeof(path), "%s/lab_footer_r3_panel_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);
#endif
    }

    remove("scratch_srv17.bin");
    free(fb);
    x4_font_close();
    printf("PASS capture_tarea17_screens\n");
    return 0;
}
