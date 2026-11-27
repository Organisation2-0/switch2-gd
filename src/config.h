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

// one icon per form of the player
enum { IC_CUBE, IC_SHIP, IC_BALL, IC_UFO, IC_WAVE, IC_ROBOT, IC_SPIDER, IC_SWING, IC_COUNT };
extern const char *const g_icon_types[IC_COUNT];   // file names in Resources/icons

typedef struct {
    Binding bind[ACT_COUNT];
    int icon[IC_COUNT];   // numbers of the chosen icons
    int col1, col2;   // palette indices
    int music, sfx;   // volume of music and sound effects, 0..100 (both 0: no sound device)
    char name[24];    // shown under the profile button of the title screen
    // graphics
    int height;       // pixel height of the picture, 16:9
    int fullscreen;   // PC
    int vsync;
    int fps;          // Switch 2: 60 or 120
    int show_fps;     // counter in the corner
    int glow, particles, shake;   // effects
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
