/**
 * \file ui2-birth.h
 * \brief Text-based user interface for character creation
 *
 * Copyright (c) 1987 - 2015 Angband contributors
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

#ifndef UI2_BIRTH_H
#define UI2_BIRTH_H

void ui_init_birthstate_handlers(void);
int textui_do_birth(void);

/* phantom */
extern bool arg_force_name;

#endif /* UI2_BIRTH_H */
