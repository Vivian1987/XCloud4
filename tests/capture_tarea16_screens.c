/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/auth/release_check.h"
#include "../src/auth/http_client.h"
#include "../src/ui/screen.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/picture_settings.h"
#include "../src/ui/server_settings.h"
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

    enum X4Lang langs[2] = {X4_LANG_ES, X4_LANG_EN};
    const char *lang_suffixes[2] = {"es", "en"};
    const char *locales[2] = {"es-ES", "en-US"};
    const char *markets[2] = {"ES", "US"};

    /* Sample mock regions with real game RTT */
    X4ServerRegion sample_regions[5];
    memset(sample_regions, 0, sizeof(sample_regions));
    snprintf(sample_regions[0].name, sizeof(sample_regions[0].name), "BrazilSouth");
    snprintf(sample_regions[0].host, sizeof(sample_regions[0].host), "brazilsouth.gssv-play-prod.xboxlive.com");
    sample_regions[0].is_default = true;
    sample_regions[0].last_game_ms = 38;

    snprintf(sample_regions[1].name, sizeof(sample_regions[1].name), "CentralUS");
    snprintf(sample_regions[1].host, sizeof(sample_regions[1].host), "centralus.gssv-play-prod.xboxlive.com");
    sample_regions[1].is_default = false;
    sample_regions[1].last_game_ms = 72;

    snprintf(sample_regions[2].name, sizeof(sample_regions[2].name), "EastUS");
    snprintf(sample_regions[2].host, sizeof(sample_regions[2].host), "eastus.gssv-play-prod.xboxlive.com");
    sample_regions[2].is_default = false;
    sample_regions[2].last_game_ms = 85;

    snprintf(sample_regions[3].name, sizeof(sample_regions[3].name), "WestEurope");
    snprintf(sample_regions[3].host, sizeof(sample_regions[3].host), "westeurope.gssv-play-prod.xboxlive.com");
    sample_regions[3].is_default = false;
    sample_regions[3].last_game_ms = 142;

    snprintf(sample_regions[4].name, sizeof(sample_regions[4].name), "UKSouth");
    snprintf(sample_regions[4].host, sizeof(sample_regions[4].host), "uksouth.gssv-play-prod.xboxlive.com");
    sample_regions[4].is_default = false;
    sample_regions[4].last_game_ms = 0; /* unmeasured */

    for (int l = 0; l < 2; ++l) {
        x4_lang_set(langs[l], locales[l], markets[l]);
        const char *sfx = lang_suffixes[l];

        /* 1. Main menu with Account card hint (no saved server choice) */
        x4_server_settings_set_path("scratch_no_choice.bin");
        remove("scratch_no_choice.bin");
        x4_server_settings_init();
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        snprintf(path, sizeof(path), "%s/main_menu_account_hint_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 2. Server tab with regions list and real game ms */
        x4_server_settings_update_regions(sample_regions, 5);
        x4_server_settings_set_selected("", true); /* Auto */
        x4_server_settings_clear_not_available();

        X4PictureSettingsUi ui = {.open = true, .tab = 1, .server_selected = 0};
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/server_tab_list_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 3. Server tab without list (before login / no saved list) */
        x4_server_settings_set_path("scratch_empty_list.bin");
        remove("scratch_empty_list.bin");
        x4_server_settings_init();
        ui.server_selected = 0;
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/server_tab_no_list_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 5. Server tab with server unavailable warning */
        x4_server_settings_update_regions(sample_regions, 5);
        /* Artificially simulate unavailable flag */
        x4_server_settings_set_selected("NonExistentServer", true);
        x4_server_settings_update_regions(sample_regions, 5); /* triggers server_unavailable_flag */
        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_screen_draw(&screen_home, &ctrl, fb);
        x4_picture_settings_ui_draw(&ui, fb);
        snprintf(path, sizeof(path), "%s/server_tab_unavailable_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);

        /* 6. First-time server selection modal in real flow (catalog loading with busy auth) */
        x4_server_settings_clear_not_available();
        x4_server_settings_set_awaiting_first_choice(true);
        X4PictureSettingsUi first_ui = {0};
        x4_picture_settings_open_server_first_time(&first_ui);

        X4CatalogSnapshot cat = {.state = X4_CATALOG_LOADING, .count = 0};
        X4CatalogBrowser cat_b = {0};
        X4RecentGames recent = {0};
        X4CatalogGuiStatus gui = {ctrl.data.connected, 1, 1, ""};

        memset(fb, 0, 1920 * 1080 * sizeof(uint32_t));
        x4_catalog_gui_draw(&cat, &cat_b, &recent, NULL, &gui, fb);
        x4_picture_settings_ui_draw(&first_ui, fb);
        snprintf(path, sizeof(path), "%s/server_tab_first_time_%s.ppm", out_dir, sfx);
        write_ppm(path, fb);
        x4_server_settings_set_awaiting_first_choice(false);
    }

    remove("scratch_no_choice.bin");
    remove("scratch_empty_list.bin");
    free(fb);
    x4_font_close();
    printf("PASS capture_tarea16_screens\n");
    return 0;
}
