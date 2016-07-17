/**
   \file ui-map.h
   \brief Writing level map info to the screen
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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

#ifndef UI2_MAP_H
#define UI2_MAP_H

extern void grid_data_as_text(struct grid_data *g,
		uint32_t *ap, wchar_t *cp, uint32_t *tap, wchar_t *tcp);
extern void move_cursor_relative(int y, int x);
extern void print_rel(uint32_t a, wchar_t c, int y, int x);

#endif /* UI2_MAP_H */
extern void prt_map(void);
extern void display_map(int *cy, int *cx);
extern void do_cmd_view_map(void);
