// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

const char *const g_action_names[ACT_COUNT] = {"Jump", "Pause", "Restart", "Back", "Confirm", "Left", "Right"};

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

void config_defaults(Config *c)
{
    config_reset_bindings(c);
    c->icon_cube = 1;
    c->icon_ship = 1;
    c->col1 = 6;   // lime
    c->col2 = 12;  // cyan
}

int config_save(const Config *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "icon_cube=%d\nicon_ship=%d\ncol1=%d\ncol2=%d\n", c->icon_cube, c->icon_ship, c->col1, c->col2);
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
        if (sscanf(line, "icon_cube=%d", &v[0]) == 1) c->icon_cube = v[0];
        else if (sscanf(line, "icon_ship=%d", &v[0]) == 1) c->icon_ship = v[0];
        else if (sscanf(line, "col1=%d", &v[0]) == 1) c->col1 = v[0];
        else if (sscanf(line, "col2=%d", &v[0]) == 1) c->col2 = v[0];
        else if (sscanf(line, "bind_%d=%d,%d,%d,%d", &idx, &v[0], &v[1], &v[2], &v[3]) == 5 && idx >= 0 &&
                 idx < ACT_COUNT) {
            c->bind[idx].key[0] = v[0]; c->bind[idx].key[1] = v[1];
            c->bind[idx].joy[0] = v[2]; c->bind[idx].joy[1] = v[3];
        }
    }
    fclose(f);
    if (c->col1 < 0 || c->col1 >= palette_size()) c->col1 = 6;
    if (c->col2 < 0 || c->col2 >= palette_size()) c->col2 = 12;
    if (c->icon_cube < 1) c->icon_cube = 1;
    if (c->icon_ship < 1) c->icon_ship = 1;
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

// ---- palette: 24 hues, 12 dark hues, 8 greys, 4 pastels (approximation of the game's colours)

int palette_size(void) { return 48; }

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

void palette_color(int i, float rgb[3])
{
    if (i < 0) i = 0;
    if (i < 24) hsv(i * 15.0f, 1, 1, rgb);
    else if (i < 36) hsv((i - 24) * 30.0f, 1, 0.55f, rgb);
    else if (i < 44) { float v = 255.0f - (i - 36) * 36.0f; rgb[0] = rgb[1] = rgb[2] = v; }
    else hsv((i - 44) * 90.0f, 0.45f, 1, rgb);
}
