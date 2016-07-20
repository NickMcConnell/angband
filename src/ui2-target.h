/**
 * \file ui-target.h
 * \brief UI for targetting code
 *
 * Copyright (c) 1997-2014 Angband contributors
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */


#ifndef UI2_TARGET_H
#define UI2_TARGET_H

#include "ui2-event.h"

/**
 * Convert a mouse event into a location (y coordinate)
 */
#define EVENT_GRID_Y(e) \
  ((int) ((e).mouse.y + angband_cave.offset_y))

/**
 * Convert a mouse event into a location (x coordinate)
 */
#define EVENT_GRID_X(e) \
	((int) ((e).mouse.x + angband_cave.offset_x))


/**
 * Height of the help screen; any higher than 4 will overlap the health
 * bar which we want to keep in targeting mode.
 */
#define HELP_HEIGHT 3

/**
 * Size of the array that is used for object names during targeting.
 */
#define TARGET_OUT_VAL_SIZE 256

int target_dir(struct keypress ch);
int target_dir_allow(struct keypress ch, bool allow_5);
void target_display_help(bool monster, bool free);
void textui_target(void);
void textui_target_closest(void);
bool target_set_interactive(int mode, int x, int y);

#endif /* UI2_TARGET_H */
