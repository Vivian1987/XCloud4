/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define X4_SERVER_MAX_REGIONS 32
#define X4_SERVER_RTT_UNMEASURED 0

typedef struct {
    char name[64];
    char api_host[128];
    char net_test_host[128];
    char host[128]; /* compatibility alias for api_host */
    char base_uri[256];
    bool is_default;
    int last_game_ms; /* 1..2000 = measured in last game, 0 = unmeasured */
    char api_ip[64];
    char net_test_ip[64];
    char other_fields[256]; /* Other JSON fields sent by Xbox for this region */
} X4ServerRegion;

/* Initialization and persistence */
void x4_server_settings_init(void);
void x4_server_settings_set_path(const char *path);

/* User choice status and mutation */
bool x4_server_settings_has_saved_choice(void);
const char *x4_server_settings_get_selected(void);
bool x4_server_settings_set_selected(const char *name, bool save_choice);

/* Region list inspection and update from Xbox /v2/login/user */
unsigned x4_server_settings_get_regions(X4ServerRegion *out, unsigned max_out);
unsigned x4_server_settings_get_region_count(void);
bool x4_server_settings_get_region(unsigned index, X4ServerRegion *out);
bool x4_server_settings_get_region_name(unsigned index, char *out, size_t cap);
int x4_server_settings_find_index(const char *name);
bool x4_server_settings_update_regions(const X4ServerRegion *regions, unsigned count);

/* Game session RTT recording */
bool x4_server_settings_record_game_rtt(const char *region_name, int rtt_ms);
int x4_server_settings_get_region_rtt(const char *region_name);

/* Display names and helpers */
const char *x4_server_settings_active_display_name(void);
const char *x4_server_settings_default_region_name(void);
const char *x4_server_region_display_name(const char *raw_name);

/* Server unavailable warning flag (saved server missing from Xbox response) */
bool x4_server_settings_server_not_available(void);
void x4_server_settings_clear_not_available(void);

/* First-time selection synchronization */
bool x4_server_settings_awaiting_first_choice(void);
void x4_server_settings_set_awaiting_first_choice(bool awaiting);
void x4_server_settings_notify_choice_done(void);
bool x4_server_settings_first_choice_completed(void);
void x4_server_settings_mark_ui_drawn(void);
bool x4_server_settings_is_ui_drawn(void);
void x4_server_settings_reset_ui_drawn(void);

/* Host and name validators */
bool x4_server_settings_validate_host(const char *host);
bool x4_server_settings_validate_name(const char *name);

/* Session JSON telemetry serialization */
void x4_server_settings_write_json(FILE *f);
