/**
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007-9 Andi Sidwell, Chris Carr, Ed Graham, Erik Osheim
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband License":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#ifndef INCLUDED_BORG_STORE_H
#define INCLUDED_BORG_STORE_H

/*
 * must be included before ALLOW_BORG to avoid empty compilation unit
 */
#include "../angband.h"

#ifdef ALLOW_BORG

#include "borg-item.h"

/*
 * the store number of HOME
 */
#define BORG_HOME 7

extern const char *SHOP_MENU_ITEMS;

/*
 * Forward declare
 */
typedef struct borg_shop borg_shop;

/*
 * A store
 */
struct borg_shop {
    borg_item * ware; /* Store contents */
};


/*
 * Current "shops"
 */
extern borg_shop *borg_shops;

/*
 * Saved (Safety) "shops"
 */
extern borg_shop *borg_safe_shops;

extern int borg_food_onsale; /* Are shops selling food? */
extern int borg_fuel_onsale; /* Are shops selling fuel? */

/* read store items rather than scraping the screen */
extern void borg_cheat_store(void);

/* check for the a full home */
extern bool borg_home_full(void);

/* check for the a full inventory */
extern bool borg_inventory_full(void);


/* initialize and free stores */
extern void borg_init_store(void);
extern void borg_free_store(void);

#endif
#endif
