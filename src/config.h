// SPDX-License-Identifier: GPL-3.0-or-later
// User settings: key/pad bindings and the player's skin. Stored as a small text file.
#ifndef GD_CONFIG_H
#define GD_CONFIG_H

typedef enum {
    ACT_JUMP, ACT_PAUSE, ACT_RESTART, ACT_BACK, ACT_CONFIRM, ACT_LEFT, ACT_RIGHT, ACT_COUNT
} Action;

typedef struct {
    int key[2];   // SDL keycodes, 0 = unbound
    int joy[2];   // joystick buttons, -1 = unbound
} Binding;

typedef struct {
    Binding bind[ACT_COUNT];
    int icon_cube;    // player_NN
    int icon_ship;    // ship_NN
    int col1, col2;   // palette indices
} Config;

extern const char *const g_action_names[ACT_COUNT];

void config_defaults(Config *c);
int config_load(Config *c, const char *path);
int config_save(const Config *c, const char *path);
void config_reset_bindings(Config *c);

// Returns the first action (in priority order) bound to the key / pad button, or -1.
int config_action_for_key(const Config *c, int keycode);
int config_action_for_joy(const Config *c, int button);

int palette_size(void);
void palette_color(int idx, float rgb[3]); // 0..255

#endif
