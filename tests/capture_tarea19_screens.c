/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ui/apple_ui.h"
#include "../src/ui/screen.h"
#include "../src/ui/catalog_gui.h"
#include "../src/ui/catalog_browser.h"
#include "../src/ui/font.h"
#include "../src/ui/lang.h"
#include "../src/ui/picture_settings.h"
#include "../src/ui/server_settings.h"
#include "../src/auth/http_client.h"

X4Http *x4_http_open(int *err) { if (err) *err = 0; return NULL; }
void x4_http_close(X4Http *h) { (void)h; }
int x4_http_releases_request(X4Http *h, const char *url, char *body, size_t cap,
    size_t *len, int *status, const _Atomic int *cancel)
{
    (void)h; (void)url; (void)body; (void)cap; (void)len; (void)status; (void)cancel;
    return -1;
}

struct X4CatalogAssets {
    X4CatalogAssetView entries[8];
    unsigned count;
};

int x4_catalog_assets_acquire(X4CatalogAssets *a, const char *id, X4CatalogAssetView *out)
{
    if (!a || !id || !out) return 0;
    for (unsigned i = 0; i < a->count; ++i) {
        if (!strcmp(id, a->entries[i].metadata.product_id)) {
            *out = a->entries[i];
            out->slot = i;
            return 1;
        }
    }
    return 0;
}

void x4_catalog_assets_release(X4CatalogAssets *a, X4CatalogAssetView *v)
{
    (void)a;
    (void)v;
}

/* Neutral procedural landscape generator (zero copyrighted game art) */
static uint32_t *make_neutral_landscape(unsigned w, unsigned h, unsigned palette)
{
    static const uint32_t sky_top[6] = {
        X4_RGB(0x1A, 0x2E, 0x4C), X4_RGB(0x1E, 0x22, 0x46), X4_RGB(0x14, 0x38, 0x3E),
        X4_RGB(0x2E, 0x1E, 0x3A), X4_RGB(0x1A, 0x33, 0x2A), X4_RGB(0x2C, 0x24, 0x1C)
    };
    static const uint32_t sky_horizon[6] = {
        X4_RGB(0x3E, 0x6B, 0x9C), X4_RGB(0x5A, 0x4E, 0x8C), X4_RGB(0x2E, 0x84, 0x8A),
        X4_RGB(0x8C, 0x4E, 0x6E), X4_RGB(0x3B, 0x7A, 0x5E), X4_RGB(0x8A, 0x62, 0x3E)
    };
    static const uint32_t ridge_mid[6] = {
        X4_RGB(0x16, 0x28, 0x40), X4_RGB(0x20, 0x1D, 0x3A), X4_RGB(0x12, 0x2C, 0x32),
        X4_RGB(0x28, 0x19, 0x32), X4_RGB(0x14, 0x28, 0x20), X4_RGB(0x26, 0x1D, 0x16)
    };
    static const uint32_t ridge_fg[6] = {
        X4_RGB(0x0D, 0x17, 0x26), X4_RGB(0x12, 0x10, 0x24), X4_RGB(0x0B, 0x1A, 0x1E),
        X4_RGB(0x17, 0x0E, 0x1E), X4_RGB(0x0C, 0x18, 0x12), X4_RGB(0x16, 0x11, 0x0D)
    };

    unsigned p_idx = palette % 6u;
    uint32_t *buf = malloc((size_t)w * h * sizeof(uint32_t));
    assert(buf != NULL);

    uint32_t c0 = sky_top[p_idx];
    uint32_t c1 = sky_horizon[p_idx];
    uint32_t c2 = ridge_mid[p_idx];
    uint32_t c3 = ridge_fg[p_idx];

    for (unsigned y = 0; y < h; ++y) {
        unsigned t = (y * 255u) / (h > 1 ? h - 1 : 1);
        uint32_t sr = ((c0 & 0xFFu)         * (255u - t) + (c1 & 0xFFu)         * t) / 255u;
        uint32_t sg = (((c0 >> 8)  & 0xFFu) * (255u - t) + ((c1 >> 8)  & 0xFFu) * t) / 255u;
        uint32_t sb = (((c0 >> 16) & 0xFFu) * (255u - t) + ((c1 >> 16) & 0xFFu) * t) / 255u;

        for (unsigned x = 0; x < w; ++x) {
            float fx = (float)x / (float)w;
            float r1 = 0.52f + 0.10f * sinf(fx * 5.2f + (float)p_idx * 1.1f)
                             + 0.05f * cosf(fx * 11.0f + (float)p_idx);
            float r2 = 0.68f + 0.08f * cosf(fx * 4.4f + (float)p_idx * 2.0f)
                             + 0.04f * sinf(fx * 9.5f);
            float fy = (float)y / (float)h;
            uint32_t col = X4_RGB(sr, sg, sb);
            if (fy > r2) {
                col = c3;
            } else if (fy > r1) {
                col = c2;
            } else {
                /* Soft sun/moon glow in sky */
                float sx = 0.72f - 0.08f * (float)(p_idx % 3);
                float sy = 0.28f;
                float dx = (fx - sx) * ((float)w / (float)h);
                float dy = fy - sy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < 0.18f) {
                    float glow = (0.18f - dist) / 0.18f;
                    uint32_t add = (uint32_t)(glow * glow * 64.0f);
                    uint32_t nr = sr + add > 255u ? 255u : sr + add;
                    uint32_t ng = sg + add > 255u ? 255u : sg + add;
                    uint32_t nb = sb + add > 255u ? 255u : sb + add;
                    col = X4_RGB(nr, ng, nb);
                }
            }
            buf[y * w + x] = col;
        }
    }
    return buf;
}

static void save_ppm(const char *dir, const char *name, const uint32_t *p)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    fprintf(f, "P6\n1920 1080\n255\n");
    for (size_t i = 0; i < 1920 * 1080; ++i) {
        unsigned char rgb[3] = {
            (unsigned char)(p[i]         & 0xFFu),
            (unsigned char)((p[i] >> 8)  & 0xFFu),
            (unsigned char)((p[i] >> 16) & 0xFFu)
        };
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    fclose(f);
    printf("Saved %s\n", path);
}

static void populate_localized_fixtures(X4CatalogSnapshot *cat, struct X4CatalogAssets *assets, enum X4Lang lang)
{
    static const char *const names_es[6] = {
        "Horizonte Boreal",
        "Odisea Estelar",
        "Cumbres de Cristal",
        "Circuito Nocturno",
        "Valle Esmeralda",
        "Ciudadela Solar"
    };
    static const char *const names_en[6] = {
        "Boreal Horizon",
        "Stellar Odyssey",
        "Crystal Peaks",
        "Night Circuit",
        "Emerald Valley",
        "Solar Citadel"
    };
    static const char *const descs_es[6] = {
        "Explora paisajes árticos y montañas infinitas en una travesía atmosférica inolvidable. "
        "Enfréntate a las inclemencias del tiempo mientras descubres los vestigios de una expedición legendaria. "
        "Utiliza herramientas de escalada avanzadas para abrirte paso por grietas heladas y glaciares traicioneros. "
        "Cada cumbre alcanzada revela nuevas rutas y secretos ocultos bajo el manto de nieve perpetua.",
        "Viaja entre sistemas estelares lejanos y descubre antiguas estaciones orbitales.",
        "Escala cumbres heladas con física realista y clima dinámico en tiempo real.",
        "Compite bajo las luces de neón en trazados urbanos de alta velocidad.",
        "Construye y protege un santuario natural lleno de misterios ancestrales.",
        "Defiende la última fortaleza luminosa en los confines del desierto."
    };
    static const char *const descs_en[6] = {
        "Explore arctic landscapes and endless mountains in an unforgettable atmospheric journey. "
        "Brave the harsh elements as you uncover the remnants of a legendary lost expedition. "
        "Use advanced climbing tools to forge your way through treacherous crevasses and frozen glaciers. "
        "Every summit reached reveals new pathways and ancient secrets buried beneath the eternal snow.",
        "Travel across distant star systems and uncover ancient orbital stations.",
        "Climb frozen summits with realistic physics and dynamic real-time weather.",
        "Race under neon lights across high-speed urban circuits.",
        "Build and protect a natural sanctuary filled with ancient mysteries.",
        "Defend the last luminous fortress at the edge of the desert."
    };
    static const char *const cats_es[6] = {
        "Aventura · Exploración", "Ciencia ficción · Acción", "Plataformas · Montaña",
        "Carreras · Arcade", "Estrategia · Naturaleza", "Acción · Táctico"
    };
    static const char *const cats_en[6] = {
        "Adventure · Exploration", "Sci-Fi · Action", "Platformer · Mountain",
        "Racing · Arcade", "Strategy · Nature", "Action · Tactical"
    };

    cat->state = X4_CATALOG_READY;
    cat->count = cat->total = 6;
    snprintf(cat->gamertag, sizeof(cat->gamertag), "Jugador4");

    for (unsigned i = 0; i < 6; ++i) {
        const char *nm = (lang == X4_LANG_EN) ? names_en[i] : names_es[i];
        const char *ds = (lang == X4_LANG_EN) ? descs_en[i] : descs_es[i];
        const char *ct = (lang == X4_LANG_EN) ? cats_en[i] : cats_es[i];

        snprintf(cat->titles[i].id, sizeof(cat->titles[i].id), "NEUTRAL_TITLE_%u", i);
        snprintf(cat->titles[i].product_id, sizeof(cat->titles[i].product_id),
            i == 1 ? "BT5P2X999VH2" : "NEUTRALPROD%02u", i);
        snprintf(cat->titles[i].name, sizeof(cat->titles[i].name), "%s", nm);
        cat->titles[i].entitled = (i != 5);

        snprintf(assets->entries[i].metadata.product_id,
            sizeof(assets->entries[i].metadata.product_id), "%s", cat->titles[i].product_id);
        snprintf(assets->entries[i].metadata.name,
            sizeof(assets->entries[i].metadata.name), "%s", nm);
        snprintf(assets->entries[i].metadata.description,
            sizeof(assets->entries[i].metadata.description), "%s", ds);
        snprintf(assets->entries[i].metadata.category,
            sizeof(assets->entries[i].metadata.category), "%s", ct);
        snprintf(assets->entries[i].metadata.publisher,
            sizeof(assets->entries[i].metadata.publisher), "Estudio Independiente");
    }
}

int main(int argc, char **argv)
{
    const char *out_dir = (argc > 1) ? argv[1] : ".";
    if (x4_font_init("assets/fonts/inter.x4font") != 0) {
        assert(x4_font_init("assets/fonts/noto_sans.x4font") == 0);
    }

    uint32_t *p = calloc(1920 * 1080, sizeof(uint32_t));
    assert(p != NULL);

    static X4CatalogSnapshot cat;
    static X4CatalogBrowser b;
    static X4RecentGames rec;
    struct X4CatalogAssets assets = {0};
    assets.count = 6;
    for (unsigned i = 0; i < 6; ++i) {
        assets.entries[i].state = X4_ASSET_READY;
        assets.entries[i].generation = 1;
        assets.entries[i].surface[X4_COVER_DETAIL].width = 512;
        assets.entries[i].surface[X4_COVER_DETAIL].height = 288; /* 16:9 */
        assets.entries[i].surface[X4_COVER_DETAIL].rgba = make_neutral_landscape(512, 288, i);

        /* Real proportions for testing uncropped contain mode (Point 3):
         * 0 & 3: 16:9 (512x288)
         * 1 & 4: 1:1 square (384x384)
         * 2 & 5: 2:3 vertical (256x384) */
        unsigned gw = 512, gh = 288;
        if (i % 3 == 1) { gw = 384; gh = 384; }
        else if (i % 3 == 2) { gw = 256; gh = 384; }
        assets.entries[i].surface[X4_COVER_GRID].width = gw;
        assets.entries[i].surface[X4_COVER_GRID].height = gh;
        assets.entries[i].surface[X4_COVER_GRID].rgba = make_neutral_landscape(gw, gh, i);
    }

    X4Controller ctrl = {0};
    ctrl.data.connected = 1;
    X4CatalogGuiStatus gui_status = { 1, 0, 0, NULL };

    for (int l = 0; l < 2; ++l) {
        enum X4Lang lang = (l == 0) ? X4_LANG_ES : X4_LANG_EN;
        const char *sfx = (l == 0) ? "es" : "en";
        if (lang == X4_LANG_ES) x4_lang_set(X4_LANG_ES, "es-MX", "MX");
        else x4_lang_set(X4_LANG_EN, "en-US", "US");

        populate_localized_fixtures(&cat, &assets, lang);
        char name[128];

        /* 1: Main menu with Fortnite card unfocused */
        X4Screen s = {0};
        s.page = 0;
        s.selected = 0; /* Explorar catalogo focused */
        s.signed_in = 1;
        X4HomeDrawContext hctx_in = {
            .signed_in = 1,
            .gamertag = "Jugador4",
            .featured_hero = &assets.entries[0].surface[X4_COVER_DETAIL],
            .fortnite_cover = &assets.entries[1].surface[X4_COVER_GRID],
        };
        x4_screen_set_home_context(&hctx_in);
        x4_screen_draw(&s, &ctrl, p);
        snprintf(name, sizeof(name), "01_main_menu_fortnite_unfocused_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 2: Main menu with Fortnite card focused */
        s.selected = 1; /* Fortnite focused */
        x4_screen_draw(&s, &ctrl, p);
        snprintf(name, sizeof(name), "02_main_menu_fortnite_focused_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 3: Discover tab with 3 tabs and recents (16:9 hero + 16:9 row) */
        x4_catalog_browser_init(&b);
        x4_catalog_browser_rebuild(&b, &cat);
        x4_recent_init(&rec);
        char r_ids[5][96] = {
            "NEUTRAL_TITLE_0", "NEUTRAL_TITLE_2", "NEUTRAL_TITLE_3",
            "NEUTRAL_TITLE_4", "NEUTRAL_TITLE_5"
        };
        x4_recent_begin(&rec, 1, 1);
        assert(x4_recent_publish(&rec, 1, 1, r_ids, 5, 0, 200, &cat));
        x4_catalog_browser_sync_recents(&b, &rec);
        x4_catalog_browser_rebuild_featured(&b, &cat, &rec);
        b.active_tab = X4_CAT_TAB_DISCOVER;
        b.hero_focused = false;
        b.discover_row_selected = 1;
        x4_catalog_gui_draw(&cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "03_catalog_3tabs_discover_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 4: Discover tab without recents (Popular fallback, hero focused) */
        x4_recent_init(&rec);
        x4_catalog_browser_rebuild_featured(&b, &cat, &rec);
        b.active_tab = X4_CAT_TAB_DISCOVER;
        b.hero_focused = true;
        b.featured_index = 0;
        x4_catalog_gui_draw(&cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "04_catalog_discover_hero_focused_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 5: Catalog tab grid showing different aspect ratios (16:9, 1:1, 2:3) */
        b.active_tab = X4_CAT_TAB_CATALOG;
        b.section = X4_CATALOG_SECTION_CATALOG;
        b.selected = 0;
        b.detail_scroll_offset = 0;
        x4_catalog_gui_draw(&cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "05_catalog_grid_aspect_ratios_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 6: Detail panel with long description scrolled with right stick */
        b.detail_scroll_offset = 2; /* Scrolled down */
        x4_catalog_gui_draw(&cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "06_catalog_detail_scrolled_long_%s", sfx);
        save_ppm(out_dir, name, p);
        b.detail_scroll_offset = 0;

        /* 7: Search keyboard open */
        b.editing = true;
        snprintf(b.query, sizeof(b.query), l == 0 ? "HORIZONTE" : "HORIZON");
        b.keyboard_cursor = 7;
        x4_catalog_gui_draw(&cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "07_catalog_search_keyboard_%s", sfx);
        save_ppm(out_dir, name, p);
        b.editing = false;
        b.query[0] = '\0';
        x4_catalog_browser_rebuild(&b, &cat);

        /* 8: Fortnite summary with 1080p selected (index 1) and Game Pass text */
        s.page = 8;
        s.selected = 1; /* 1080p */
        x4_screen_draw(&s, &ctrl, p);
        x4_fortnite_summary_draw(&s, &cat, 0, p);
        snprintf(name, sizeof(name), "08_fortnite_summary_1080p_selected_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 9: Country unavailable large QR in Catalog */
        X4CatalogSnapshot err_cat = {0};
        err_cat.state = X4_CATALOG_ERROR;
        err_cat.error = X4_AUTH_E_XBOX;
        err_cat.http_status = 403;
        err_cat.xerr = 0;
        snprintf(err_cat.offering, sizeof(err_cat.offering), "xgpuwebf2p");
        snprintf(err_cat.stage, sizeof(err_cat.stage), "Xbox Cloud Gaming rechazo la cuenta");
        snprintf(err_cat.gamertag, sizeof(err_cat.gamertag), "Jugador4");
        x4_catalog_gui_draw(&err_cat, &b, &rec, &assets, &gui_status, p);
        snprintf(name, sizeof(name), "09_country_qr_large_catalog_%s", sfx);
        save_ppm(out_dir, name, p);

        /* 10: Country unavailable large QR in Fortnite summary */
        s.page = 8;
        s.selected = 0;
        x4_screen_draw(&s, &ctrl, p);
        x4_fortnite_summary_draw(&s, &err_cat, 0, p);
        snprintf(name, sizeof(name), "10_country_qr_large_fortnite_%s", sfx);
        save_ppm(out_dir, name, p);
    }

    for (unsigned i = 0; i < 6; ++i) {
        free(assets.entries[i].surface[X4_COVER_DETAIL].rgba);
        free(assets.entries[i].surface[X4_COVER_GRID].rgba);
    }
    free(p);
    x4_font_close();
    return 0;
}
