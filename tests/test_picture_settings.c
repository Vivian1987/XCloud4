/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ui/picture_settings.h"
#include "../src/streaming/stream_profile.h"

static void test_mapping_and_vsync(void)
{
    printf("Testing option -> profile mapping and VSync OFF...\n");
    assert(x4_picture_settings_to_profile(X4_PIC_QUALITY_1080P_ALTA) == X4_REQUEST_1080P_G);
    assert(x4_picture_settings_to_profile(X4_PIC_QUALITY_1080P) == X4_REQUEST_1080P_G8);
    assert(x4_picture_settings_to_profile(X4_PIC_QUALITY_720P) == X4_REQUEST_720P_HQ);

    /* Test stream settings integration */
    x4_picture_settings_set(X4_PIC_QUALITY_1080P_ALTA);
    X4StreamRequestConfig cfg = x4_stream_settings_get();
#if X4_PUBLIC
    assert(cfg.profile == X4_REQUEST_1080P_G);
    assert(cfg.interaction_modes == X4_INTERACTION_VSYNC_OFF);
    assert(cfg.decoder_depth == X4_DEPTH_AUTO);

    x4_picture_settings_set(X4_PIC_QUALITY_1080P);
    cfg = x4_stream_settings_get();
    assert(cfg.profile == X4_REQUEST_1080P_G8);
    assert(cfg.interaction_modes == X4_INTERACTION_VSYNC_OFF);

    x4_picture_settings_set(X4_PIC_QUALITY_720P);
    cfg = x4_stream_settings_get();
    assert(cfg.profile == X4_REQUEST_720P_HQ);
    assert(cfg.interaction_modes == X4_INTERACTION_VSYNC_OFF);
#else
    (void)cfg;
#endif
    printf("PASS: mapping and VSync\n");
}

static void test_persistence(const char *test_path)
{
    printf("Testing persistence at %s...\n", test_path);
    x4_picture_settings_set_path(test_path);
    remove(test_path);

    /* Missing file -> returns default (Alta / 0) */
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Save option 1 (1080p), re-init -> 1 */
    assert(x4_picture_settings_set(X4_PIC_QUALITY_1080P) == true);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P);

    /* Save option 2 (720p), re-init -> 2 */
    assert(x4_picture_settings_set(X4_PIC_QUALITY_720P) == true);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_720P);

    /* Save option 0 (Alta), re-init -> 0 */
    assert(x4_picture_settings_set(X4_PIC_QUALITY_1080P_ALTA) == true);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Out of bounds set -> false, active setting preserved */
    assert(x4_picture_settings_set((X4PictureQualityOption)99) == false);
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Corruption: truncated file */
    FILE *f = fopen(test_path, "wb");
    assert(f != NULL);
    uint32_t partial[2] = {0x31533458u, 1u};
    fwrite(partial, 1, sizeof(partial), f);
    fclose(f);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Corruption: bad magic */
    f = fopen(test_path, "wb");
    assert(f != NULL);
    uint32_t bad_magic[4] = {0xdeadbeefu, 1u, 1u, 0u};
    fwrite(bad_magic, 1, sizeof(bad_magic), f);
    fclose(f);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Corruption: unknown version */
    f = fopen(test_path, "wb");
    assert(f != NULL);
    uint32_t bad_ver[4] = {0x31533458u, 99u, 1u, 0u};
    fwrite(bad_ver, 1, sizeof(bad_ver), f);
    fclose(f);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Corruption: invalid quality value */
    f = fopen(test_path, "wb");
    assert(f != NULL);
    uint32_t bad_val[4] = {0x31533458u, 1u, 5u, 0u};
    fwrite(bad_val, 1, sizeof(bad_val), f);
    fclose(f);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    /* Corruption: tampered checksum */
    assert(x4_picture_settings_set(X4_PIC_QUALITY_1080P) == true);
    /* Read valid file, corrupt 1 byte of checksum */
    f = fopen(test_path, "r+b");
    assert(f != NULL);
    fseek(f, 12, SEEK_SET);
    uint8_t b = 0;
    fread(&b, 1, 1, f);
    b ^= 0xff;
    fseek(f, 12, SEEK_SET);
    fwrite(&b, 1, 1, f);
    fclose(f);
    x4_picture_settings_init();
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA);

    remove(test_path);
    printf("PASS: persistence and corruption handling\n");
}

static void test_menu_navigation(const char *test_path)
{
    printf("Testing menu navigation and guards...\n");
    x4_picture_settings_set_path(test_path);
    x4_picture_settings_set(X4_PIC_QUALITY_1080P_ALTA);

    X4PictureSettingsUi ui = {0};

    /* Guard: cannot open if can_edit is false (e.g. in game or keyboard editing) */
    bool handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, false);
    assert(!handled);
    assert(!ui.open);

    /* Open when can_edit is true */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, true);
    assert(handled);
    assert(ui.open);
    assert(ui.selected == 0); /* current saved was Alta */

    /* Navigate UP from 0 -> wraps to 2 */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_UP, true);
    assert(handled);
    assert(ui.selected == 2);

    /* Navigate UP from 2 -> 1 */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_UP, true);
    assert(handled);
    assert(ui.selected == 1);

    /* Navigate DOWN from 1 -> 2 */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_DOWN, true);
    assert(handled);
    assert(ui.selected == 2);

    /* Navigate DOWN from 2 -> wraps to 0 */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_DOWN, true);
    assert(handled);
    assert(ui.selected == 0);

    /* Cancel: closes without saving */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_UP, true); /* select 2 */
    assert(ui.selected == 2);
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_CANCEL, true);
    assert(handled);
    assert(!ui.open);
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P_ALTA); /* still 0 */

    /* Reopen: selection starts from current saved */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, true);
    assert(handled);
    assert(ui.open);
    assert(ui.selected == 0);

    /* Navigate to 1, Confirm: saves and closes */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_DOWN, true);
    assert(ui.selected == 1);
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_CONFIRM, true);
    assert(handled);
    assert(!ui.open);
    assert(x4_picture_settings_get() == X4_PIC_QUALITY_1080P);

    /* Guard: if open and can_edit suddenly becomes false, closes immediately */
    handled = x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, true);
    assert(ui.open);
    handled = x4_picture_settings_ui_handle(&ui, 0, false);
    assert(handled); /* consumed */
    assert(!ui.open);

    remove(test_path);
    printf("PASS: navigation and guards\n");
}

/* OpenOrbis pad button bitmasks for testing */
#define TEST_PAD_SELECT   (1u << 0)
#define TEST_PAD_L3       (1u << 1)
#define TEST_PAD_R3       (1u << 2)
#define TEST_PAD_START    (1u << 3)
#define TEST_PAD_UP       (1u << 4)
#define TEST_PAD_RIGHT    (1u << 5)
#define TEST_PAD_DOWN     (1u << 6)
#define TEST_PAD_LEFT     (1u << 7)
#define TEST_PAD_L2       (1u << 8)
#define TEST_PAD_R2       (1u << 9)
#define TEST_PAD_L1       (1u << 10)
#define TEST_PAD_R1       (1u << 11)
#define TEST_PAD_TRIANGLE (1u << 12)
#define TEST_PAD_CIRCLE   (1u << 13)
#define TEST_PAD_CROSS    (1u << 14)
#define TEST_PAD_SQUARE   (1u << 15)
#define TEST_PAD_OPTIONS  (1u << 3)

static void test_button_suppression_and_dimming(void)
{
    printf("Testing button suppression behind open modal dialog...\n");
    X4PictureSettingsUi ui = {0};

    /* 1. Modal closed: buttons pass through normally */
    uint32_t controller_pressed = TEST_PAD_CROSS;
    uint32_t ui_pressed = controller_pressed;
    unsigned pic_actions = 0;
    if (controller_pressed & TEST_PAD_OPTIONS) pic_actions |= X4_PIC_SETTINGS_OPEN;
    if (controller_pressed & TEST_PAD_UP) pic_actions |= X4_PIC_SETTINGS_UP;
    if (controller_pressed & TEST_PAD_DOWN) pic_actions |= X4_PIC_SETTINGS_DOWN;
    if (controller_pressed & TEST_PAD_CROSS) pic_actions |= X4_PIC_SETTINGS_CONFIRM;
    if (controller_pressed & TEST_PAD_CIRCLE) pic_actions |= X4_PIC_SETTINGS_CANCEL;
    bool pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(!ui.open);
    assert(!pic_consumed);
    assert(controller_pressed == TEST_PAD_CROSS); /* Not suppressed when closed */
    assert(ui_pressed == TEST_PAD_CROSS);

    /* 2. Open modal with OPTIONS */
    controller_pressed = TEST_PAD_OPTIONS;
    ui_pressed = controller_pressed;
    pic_actions = X4_PIC_SETTINGS_OPEN;
    pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(pic_consumed);
    assert(ui.open);
    assert(controller_pressed == 0); /* Suppressed on opening frame! */
    assert(ui_pressed == 0);

    /* 3. With modal open: test CROSS (Confirm/Save) */
    /* Must be consumed by modal and suppressed for Account/Catalog behind */
    controller_pressed = TEST_PAD_CROSS;
    ui_pressed = controller_pressed;
    pic_actions = X4_PIC_SETTINGS_CONFIRM;
    pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(pic_consumed);
    assert(!ui.open); /* Confirmed and closed */
    assert(controller_pressed == 0); /* Behind pages NEVER see CROSS! */
    assert(ui_pressed == 0);

    /* 4. Reopen and test CIRCLE (Cancel) */
    x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, true);
    assert(ui.open);
    controller_pressed = TEST_PAD_CIRCLE;
    ui_pressed = controller_pressed;
    pic_actions = X4_PIC_SETTINGS_CANCEL;
    pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(pic_consumed);
    assert(!ui.open);
    assert(controller_pressed == 0); /* Behind pages NEVER see CIRCLE! */
    assert(ui_pressed == 0);

    /* 5. Reopen and test UP / DOWN (Navigation inside modal) */
    x4_picture_settings_ui_handle(&ui, X4_PIC_SETTINGS_OPEN, true);
    assert(ui.open);
    controller_pressed = TEST_PAD_UP;
    ui_pressed = controller_pressed;
    pic_actions = X4_PIC_SETTINGS_UP;
    pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(pic_consumed);
    assert(ui.open);
    assert(controller_pressed == 0); /* Catalog selection does NOT move! */
    assert(ui_pressed == 0);

    controller_pressed = TEST_PAD_DOWN;
    ui_pressed = controller_pressed;
    pic_actions = X4_PIC_SETTINGS_DOWN;
    pic_consumed = x4_picture_settings_ui_handle(&ui, pic_actions, true);
    if (pic_consumed || ui.open) {
        ui_pressed = 0;
        controller_pressed = 0;
    }
    assert(pic_consumed);
    assert(ui.open);
    assert(controller_pressed == 0); /* Catalog selection does NOT move! */
    assert(ui_pressed == 0);

    /* 6. Verify background dimming */
    printf("Testing background dimming in x4_picture_settings_ui_draw...\n");
    uint32_t *fb = calloc(1920 * 1080, sizeof(uint32_t));
    assert(fb != NULL);
    /* Set distinct colors outside dialog box (top-left 10,10) and inside (500,500) */
    fb[10 + 10 * 1920] = 0xffa0b0c0;
    fb[500 + 500 * 1920] = 0xffffffff;

    /* When ui is open, draw must dim background by 50% */
    assert(ui.open);
    x4_picture_settings_ui_draw(&ui, fb);

    uint32_t expected_scrim = (((0xffa0b0c0 >> 1) & 0x007f7f7f) | 0xff000000);
    assert(fb[10 + 10 * 1920] == expected_scrim);

    /* Inside dialog box, pixel is covered by dialog background / cards */
    assert(fb[500 + 500 * 1920] != 0xffffffff);
    assert(fb[500 + 500 * 1920] != (((0xffffffff >> 1) & 0x007f7f7f) | 0xff000000));

    free(fb);
    printf("PASS: button suppression and background dimming\n");
}

int main(void)
{
    test_mapping_and_vsync();
    test_persistence("test_pic_settings.bin");
    test_menu_navigation("test_pic_settings.bin");
    test_button_suppression_and_dimming();
    puts("ALL test_picture_settings PASSED");
    return 0;
}
