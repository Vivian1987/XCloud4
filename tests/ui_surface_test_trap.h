/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "../src/ui/ui_surface.h"

typedef struct {
    uint32_t *destination[X4_UI_SCANOUTS]; /* PROT_NONE, never dereference */
    uint32_t *mirror[X4_UI_SCANOUTS];       /* hidden normal-memory stores */
    uint32_t *guarded[X4_UI_SCANOUTS];
    uint64_t stores,pixels_stored;
} X4UiTestTrap;
void x4_ui_test_trap_open(X4UiTestTrap *trap);
void x4_ui_test_trap_close(X4UiTestTrap *trap);
void x4_ui_test_trap_assert_guards(const X4UiTestTrap *trap);
void x4_ui_test_trap_positive_read(const X4UiTestTrap *trap);
