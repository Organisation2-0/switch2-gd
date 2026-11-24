// SPDX-License-Identifier: GPL-3.0-or-later
// Object id -> sprite frame names. The mapping lives in the game binary, not in
// the Resources folder, so it is built up here and checked against renders.
#ifndef GD_OBJECTS_H
#define GD_OBJECTS_H

// Returns the main sprite frame for an object id, or NULL when unknown.
const char *gd_object_frame(int id);

#endif
