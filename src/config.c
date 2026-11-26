// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

const char *const g_action_names[ACT_COUNT] = {"Jump", "Pause", "Restart", "Back", "Confirm", "Left", "Right"};

const char *const g_icon_types[IC_COUNT] = {"player", "ship", "player_ball", "bird",
                                          "dart", "robot", "spider", "swing"};
static const char *const k_icon_keys[IC_COUNT] = {"icon_cube", "icon_ship", "icon_ball", "icon_ufo",
                                                  "icon_wave", "icon_robot", "icon_spider", "icon_swing"};

void config_reset_bindings(Config *c)
{
    for (int i = 0; i < ACT_COUNT; i++) {
        c->bind[i].key[0] = c->bind[i].key[1] = 0;
        c->bind[i].joy[0] = c->bind[i].joy[1] = -1;
    }
    // Pad numbers follow the Switch layout: 0 A, 1 B, 2 X, 3 Y, 4 L3, 5 R3, 6 L, 7 R,
    // 8 ZL, 9 ZR, 10 +, 11 -, 12 left, 13 up, 14 right, 15 down.
    c->bind[ACT_JUMP] = (Binding){{SDLK_SPACE, SDLK_UP}, {0, 2}};
    c->bind[ACT_PAUSE] = (Binding){{SDLK_p, 0}, {10, -1}};
    c->bind[ACT_RESTART] = (Binding){{SDLK_r, 0}, {11, -1}};
    c->bind[ACT_BACK] = (Binding){{SDLK_ESCAPE, SDLK_BACKSPACE}, {1, -1}};
    c->bind[ACT_CONFIRM] = (Binding){{SDLK_RETURN, 0}, {0, -1}};
    c->bind[ACT_LEFT] = (Binding){{SDLK_LEFT, SDLK_a}, {12, 6}};
    c->bind[ACT_RIGHT] = (Binding){{SDLK_RIGHT, SDLK_d}, {14, 7}};
}

static int palette_nearest(int r, int g, int b);
static void old_colour(int i, float rgb[3]);

void config_defaults(Config *c)
{
    config_reset_bindings(c);
    for (int i = 0; i < IC_COUNT; i++) c->icon[i] = 1;
    c->col1 = palette_nearest(125, 255, 0);
    c->col2 = palette_nearest(0, 255, 255);
    c->music = c->sfx = 100;
    snprintf(c->name, sizeof c->name, "Player");
}

int config_save(const Config *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    for (int i = 0; i < IC_COUNT; i++) fprintf(f, "%s=%d\n", k_icon_keys[i], c->icon[i]);
    for (int i = 0; i < 2; i++) {
        float rgb[3];
        palette_color(i ? c->col2 : c->col1, rgb);
        fprintf(f, "color%d=%02X%02X%02X\n", i + 1, (int)rgb[0], (int)rgb[1], (int)rgb[2]);
    }
    fprintf(f, "name=%s\n", c->name);
    fprintf(f, "music=%d\nsfx=%d\n", c->music, c->sfx);
    for (int i = 0; i < ACT_COUNT; i++)
        fprintf(f, "bind_%d=%d,%d,%d,%d\n", i, c->bind[i].key[0], c->bind[i].key[1], c->bind[i].joy[0],
                c->bind[i].joy[1]);
    fclose(f);
    return 0;
}

int config_load(Config *c, const char *path)
{
    config_defaults(c);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[160];
    while (fgets(line, sizeof line, f)) {
        int v[4], idx;
        unsigned rgbx;
        int icon = 0;
        for (; icon < IC_COUNT; icon++) {
            const size_t n = strlen(k_icon_keys[icon]);
            if (!strncmp(line, k_icon_keys[icon], n) && line[n] == '=') break;
        }
        if (icon < IC_COUNT) c->icon[icon] = atoi(strchr(line, '=') + 1);
        else if (sscanf(line, "col%d=%d", &idx, &v[0]) == 2 && (idx == 1 || idx == 2)) {   // old files
            float rgb[3];
            old_colour(v[0], rgb);
            const int n = palette_nearest((int)rgb[0], (int)rgb[1], (int)rgb[2]);
            if (idx == 1) c->col1 = n; else c->col2 = n;
        }
        else if (sscanf(line, "color%d=%x", &idx, &rgbx) == 2 && (idx == 1 || idx == 2)) {
            const int n = palette_nearest((int)(rgbx >> 16) & 255, (int)(rgbx >> 8) & 255, (int)rgbx & 255);
            if (idx == 1) c->col1 = n; else c->col2 = n;
        }
        else if (sscanf(line, "sfx=%d", &v[0]) == 1) c->sfx = v[0] < 0 ? 0 : v[0] > 100 ? 100 : v[0];
        else if (sscanf(line, "music=%d", &v[0]) == 1) c->music = v[0] < 0 ? 0 : v[0] > 100 ? 100 : v[0];
        else if (!strncmp(line, "name=", 5)) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(c->name, sizeof c->name, "%.23s", line + 5);
        }
        else if (sscanf(line, "bind_%d=%d,%d,%d,%d", &idx, &v[0], &v[1], &v[2], &v[3]) == 5 && idx >= 0 &&
                 idx < ACT_COUNT) {
            c->bind[idx].key[0] = v[0]; c->bind[idx].key[1] = v[1];
            c->bind[idx].joy[0] = v[2]; c->bind[idx].joy[1] = v[3];
        }
    }
    fclose(f);
    for (int i = 0; i < IC_COUNT; i++)
        if (c->icon[i] < 1) c->icon[i] = 1;
    return 0;
}

int config_action_for_key(const Config *c, int key)
{
    if (!key) return -1;
    for (int a = 0; a < ACT_COUNT; a++)
        if (c->bind[a].key[0] == key || c->bind[a].key[1] == key) return a;
    return -1;
}

int config_action_for_joy(const Config *c, int btn)
{
    if (btn < 0) return -1;
    for (int a = 0; a < ACT_COUNT; a++)
        if (c->bind[a].joy[0] == btn || c->bind[a].joy[1] == btn) return a;
    return -1;
}

// ---- the player colours of the original, in the order of its colour page: eight groups of four
// columns (the sixth has four rows, the others three) and a row of greys

static const unsigned int k_palette[] = {
    0xFDD4CE, 0xFF7D7D, 0xFF3A3A, 0xFF0000, 0x960000, 0x700000, 0x520200, 0x380106, 0xAF004B, 0x804F4F, 0x7A3535, 0x512424,
    0xFFB972, 0xFFA040, 0xFF7D00, 0xFF4B00, 0xAF4B00, 0xA36246, 0x754936, 0x563528, 0x963200, 0x66311E, 0x5B2700, 0x472000,
    0xFFFFC0, 0xFFFA7F, 0xFFFF00, 0x7D7D00, 0xFDE0A0, 0xFFB900, 0x966400, 0x50320E, 0xCDA576, 0xA77B4D, 0x6D5339, 0x513E2A,
    0xC0FFA0, 0xB1FF6D, 0x7DFF00, 0x00FF00, 0xD2FF32, 0x4BAF00, 0x649600, 0x00AF4B, 0x00FF7D, 0x009600, 0x006000, 0x004000,
    0xC0FFE0, 0x94FFE4, 0x00FFC0, 0x00FFFF, 0x7DFFAF, 0x43A18A, 0x316D5F, 0x265449, 0x009664, 0x007D7D, 0x006060, 0x004040,
    0xA0FFFF, 0x00C8FF, 0x007DFF, 0x0000FF, 0x004BAF, 0x000096, 0x010770, 0x000A4C, 0x006496, 0x00496D, 0x00324C, 0x002638,
    0x76BDFF, 0x5080AD, 0x335375, 0x233C56,
    0xBEB5FF, 0x7D7DFF, 0x7D00FF, 0x640096, 0xB680FF, 0x4B00AF, 0x3D068C, 0x370860, 0x4D4D8F, 0x6F49A4, 0x54367F, 0x422A63,
    0xFCB5FF, 0xFF007D, 0x960064, 0x66033E, 0xFF00FF, 0xB900FF, 0x7D007D, 0x470134, 0xFA7FFF, 0xAF57AF, 0x824382, 0x5E315E,
    0xFFFFFF, 0xE0E0E0, 0xAFAFAF, 0x808080, 0x5A5A5A, 0x404040, 0x000000};

int palette_size(void) { return (int)(sizeof k_palette / sizeof k_palette[0]); }

static int palette_nearest(int r, int g, int b)
{
    int best = 0;
    long far = -1;
    for (int i = 0; i < palette_size(); i++) {
        const int dr = r - (int)(k_palette[i] >> 16), dg = g - (int)((k_palette[i] >> 8) & 255),
                  db = b - (int)(k_palette[i] & 255);
        const long d = (long)dr * dr + (long)dg * dg + (long)db * db;
        if (far < 0 || d < far) { far = d; best = i; }
    }
    return best;
}

// the 48 colours of versions up to 1.1.0, whose numbers older config files hold
static void hsv(float h, float s, float v, float rgb[3])
{
    float c = v * s, hp = fmodf(h, 360.0f) / 60.0f;
    float x = c * (1 - fabsf(fmodf(hp, 2.0f) - 1));
    float r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    float m = v - c;
    rgb[0] = (r + m) * 255; rgb[1] = (g + m) * 255; rgb[2] = (b + m) * 255;
}

static void old_colour(int i, float rgb[3])
{
    if (i < 0 || i >= 48) i = 6;
    if (i < 24) hsv(i * 15.0f, 1, 1, rgb);
    else if (i < 36) hsv((i - 24) * 30.0f, 1, 0.55f, rgb);
    else if (i < 44) { float v = 255.0f - (i - 36) * 36.0f; rgb[0] = rgb[1] = rgb[2] = v; }
    else hsv((i - 44) * 90.0f, 0.45f, 1, rgb);
}

void palette_color(int i, float rgb[3])
{
    if (i < 0 || i >= palette_size()) i = 0;
    rgb[0] = (float)(k_palette[i] >> 16);
    rgb[1] = (float)((k_palette[i] >> 8) & 255);
    rgb[2] = (float)(k_palette[i] & 255);
}
