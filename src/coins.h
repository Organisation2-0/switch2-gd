// SPDX-License-Identifier: GPL-3.0-or-later
// The coins the player owns, per level, kept in a small text file.
#ifndef GD_COINS_H
#define GD_COINS_H

void coins_load(const char *path);
int coins_owned(const char *level_key);   // bit 0: the level's first coin
int coins_count(const char *level_key);   // coins the level has, 0 if not known yet
// Adds the coins of `mask` to those of the level and notes how many it has; saves the file if that
// changed anything.
void coins_store(const char *level_key, int mask, int count);

#endif
