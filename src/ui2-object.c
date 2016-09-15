/**
 * \file ui2-object.c
 * \brief Object lists and selection, and other object-related UI functions
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 * Copyright (c) 2007-9 Andi Sidwell, Chris Carr, Ed Graham, Erik Osheim
 * Copyright (c) 2015 Nick McConnell
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
 *    are included in all such copies. Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "cmds.h"
#include "effects.h"
#include "game-input.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui2-command.h"
#include "ui2-display.h"
#include "ui2-game.h"
#include "ui2-input.h"
#include "ui2-keymap.h"
#include "ui2-menu.h"
#include "ui2-object.h"
#include "ui2-options.h"
#include "ui2-output.h"
#include "ui2-prefs.h"

/**
 * ------------------------------------------------------------------------
 * Variables for object display and selection
 * ------------------------------------------------------------------------
 */
#define MAX_ITEMS 64

/* Label looks like this: "a) "
 * three chars + null byte */
#define OLIST_LABEL_SIZE 4

/* Equip looks like this: "On right hand : "
 * 20 bytes should be enough */
#define OLIST_EQUIP_SIZE 20

/* Name is the name of an object: "a Wooden Torch (5000 turns)" */
#define OLIST_NAME_SIZE \
	ANGBAND_TERM_STANDARD_WIDTH

#define EXTRA_FIELD_WEIGHT_WIDTH 9
#define EXTRA_FIELD_PRICE_WIDTH 9
#define EXTRA_FIELD_FAIL_WIDTH 10

/**
 * Info about a particular object
 */

struct object_menu_item_extra {
	struct {
		char str[EXTRA_FIELD_WEIGHT_WIDTH + 1];
		size_t len;
		size_t size;
	} weight;

	struct {
		char str[EXTRA_FIELD_PRICE_WIDTH + 1];
		size_t len;
		size_t size;
	} price;

	struct {
		char str[EXTRA_FIELD_FAIL_WIDTH + 1];
		size_t len;
		size_t size;
	} fail;
};

struct object_menu_item {
	struct {
		char str[OLIST_LABEL_SIZE];
		size_t len;
		size_t size;
	} label;

	struct {
		char str[OLIST_EQUIP_SIZE];
		size_t len;
		size_t size;
	} equip;

	struct {
		char str[OLIST_NAME_SIZE];
		size_t len;
		size_t size;
	} name;

	struct object_menu_item_extra extra;
	struct object *object;
	char key;
};

struct object_menu_list {
	struct object_menu_item items[MAX_ITEMS];
	size_t len;
	size_t size;
	size_t line_max_len;
	size_t total_max_len;
	size_t extra_fields_offset;
};

static void show_item_extra(const struct object_menu_item *item,
		struct loc loc, int mode)
{
	/* Price */
	if (item->extra.price.len > 0) {
		put_str_len(item->extra.price.str, loc, item->extra.price.len);
		loc.x += item->extra.price.len;
	}

	/* Failure chance for magic devices and activations */
	if (item->extra.fail.len > 0) {
		put_str_len(item->extra.fail.str, loc, item->extra.fail.len);
		loc.x += item->extra.fail.len;
	}

	/* Weight */
	if (item->extra.weight.len > 0) {
		put_str_len(item->extra.weight.str, loc, item->extra.weight.len);
		/* Just because (maybe stuff will be added later?) */
		loc.x += item->extra.weight.len;
	}
}

/**
 * Display an object. Each object may be prefixed with a label.
 * Used by show_inven(), show_equip(), show_quiver() and show_floor().
 * Mode flags are documented in object.h
 */
static void show_item(struct object_menu_item *item, 
		struct loc loc, size_t extra_fields_offset,
		bool cursor, int mode)
{
	/* Save that for later */
	struct loc saved_loc = loc;

	uint32_t attr = menu_row_style(true, cursor);

	size_t label_len = item->label.len;
	size_t equip_len = item->equip.len;
	size_t name_len  = item->name.len;

	erase_line(loc);

	if (label_len > 0) {
		c_put_str_len(attr, item->label.str, loc, label_len);
		loc.x += label_len;
	}

	if (equip_len > 0) {
		c_put_str_len(attr, item->equip.str, loc, equip_len);
		loc.x += equip_len;
	}

	/* Truncate the name if it's too long */
	if (extra_fields_offset > 0
			&& label_len + equip_len + name_len > extra_fields_offset)
	{
		if (label_len + equip_len < extra_fields_offset) {
			name_len = extra_fields_offset - label_len - equip_len;
		} else {
			name_len = 0;
		}
	}

	if (name_len > 0) {
		attr = item->object ?
			(uint32_t) item->object->kind->base->attr : menu_row_style(false, false);
		c_put_str_len(attr, item->name.str, loc, name_len);
	}

	/* No offset, no extra fields */
	if (extra_fields_offset > 0) {
		saved_loc.x += extra_fields_offset;
		show_item_extra(item, saved_loc, mode);
	}
}

/**
 * ------------------------------------------------------------------------
 * Display of lists of objects
 * ------------------------------------------------------------------------
 */

/**
 * Returns a new object list. Non-reentrant (call it optimization, or legacy)
 */
static struct object_menu_list *get_obj_list(void)
{
	static struct object_menu_list olist;

	memset(&olist, 0, sizeof(olist));

	olist.size = N_ELEMENTS(olist.items);

	for (size_t i = 0; i < olist.size; i++) {
		olist.items[i].label.size = sizeof(olist.items[i].label.str);
		olist.items[i].equip.size = sizeof(olist.items[i].equip.str);
		olist.items[i].name.size  = sizeof(olist.items[i].name.str);

		olist.items[i].extra.weight.size = sizeof(olist.items[i].extra.weight.str);
		olist.items[i].extra.price.size  = sizeof(olist.items[i].extra.price.str);
		olist.items[i].extra.fail.size   = sizeof(olist.items[i].extra.fail.str);

		/* Pedantry */
		olist.items[i].object = NULL;
	}

	return &olist;
}

static void free_obj_list(struct object_menu_list *olist)
{
	(void) olist;

	/* Maybe get_obj_list() will become reentrant at some point? */
}

static void set_item_extra(struct object_menu_list *olist, size_t i, int mode)
{
	const struct object *obj = olist->items[i].object;

	if (mode & OLIST_PRICE) {
		struct store *store = store_at(cave, player->py, player->px);
		if (store) {
			int price = price_item(store, obj, true, obj->number);

			olist->items[i].extra.price.len =
				strnfmt(olist->items[i].extra.price.str,
						olist->items[i].extra.price.size,
						"%6d au", price);
		}
	}

	/* Failure chance for magic devices and activations */
	if ((mode & OLIST_FAIL) && obj_can_fail(obj)) {
		int fail = (9 + get_use_device_chance(obj)) / 10;

		if (object_effect_is_known(obj)) {
			olist->items[i].extra.fail.len =
				strnfmt(olist->items[i].extra.fail.str,
						olist->items[i].extra.fail.size,
						"%4d%% fail", fail);
		} else {
			olist->items[i].extra.fail.len =
				my_strcpy(olist->items[i].extra.fail.str,
						"    ? fail",
						olist->items[i].extra.fail.size);
		}
	}

	/* Weight */
	if (mode & OLIST_WEIGHT) {
		int weight = obj->weight * obj->number;

		olist->items[i].extra.weight.len =
			strnfmt(olist->items[i].extra.weight.str,
					olist->items[i].extra.weight.size,
					"%4d.%1d lb", weight / 10, weight % 10);
	}
}

static void set_olist_extra(struct object_menu_list *olist, int mode)
{
	const size_t term_width = Term_width();

	/* Don't show objects weight if the subwindow is not big enought */
	if ((mode & OLIST_WINDOW)
			&& term_width < olist->line_max_len + EXTRA_FIELD_WEIGHT_WIDTH)
	{
		mode &= ~OLIST_WEIGHT;
	}

	size_t extra_fields_width = 0;
	if (mode & OLIST_WEIGHT) extra_fields_width += EXTRA_FIELD_WEIGHT_WIDTH;
	if (mode & OLIST_PRICE)  extra_fields_width += EXTRA_FIELD_PRICE_WIDTH;
	if (mode & OLIST_FAIL)   extra_fields_width += EXTRA_FIELD_FAIL_WIDTH;

	/* Column offset of the first extra field */
	if (extra_fields_width > 0
			&& extra_fields_width < term_width)
	{
		olist->extra_fields_offset =
			MIN(olist->line_max_len, term_width - extra_fields_width);

		for (size_t i = 0; i < olist->len; i++) {
			if (olist->items[i].object != NULL) {
				set_item_extra(olist, i, mode);
			}
		}
	} else {
		olist->extra_fields_offset = 0; /* No fields, no offset */
	}

	olist->total_max_len = olist->line_max_len + extra_fields_width;
}

/**
 * Set object names and get their maximum length.
 * Only makes sense after building the object list.
 */
static void set_olist_names(struct object_menu_list *olist, bool terse)
{
	int oflags =
		ODESC_PREFIX | ODESC_FULL | (terse ? ODESC_TERSE : 0);

	/* Calculate name offset and max name length */
	for (size_t i = 0; i < olist->len; i++) {
		struct object_menu_item *item = &olist->items[i];
		struct object *obj = item->object;

		if (obj == NULL) {
			item->name.len =
				my_strcpy(item->name.str, "(nothing)", item->name.size);
		} else {
			item->name.len =
				object_desc(item->name.str, item->name.size, obj, oflags);
		}

		size_t len = item->label.len + item->equip.len + item->name.len;

		olist->line_max_len = MAX(olist->line_max_len, len);
	}
}

/**
 * Build the object list.
 */
static void build_obj_list(struct object_menu_list *olist,
		struct object **objects, int last, const char *keys,
		item_tester tester, int mode)
{
	const bool quiver = (mode & OLIST_QUIVER_FULL) ? true : false;
	const bool window = (mode & OLIST_WINDOW)      ? true : false;
	const bool terse  = (mode & OLIST_TERSE)       ? true : false;
	const bool menu   = (mode & OLIST_MENU)        ? true : false;

	/* Equipment is handles specially, since there is no list for it */
	const bool equip  = (objects == NULL);

#define SHOW_OBJECT_IF_GOLD(obj) \
	((mode & OLIST_GOLD) && ((obj) != NULL) && tval_is_money(obj))

#define SHOW_OBJECT_IF_EMPTY(obj) \
	((mode & OLIST_SHOW_EMPTY) && ((obj) == NULL))

	/* Build the object list */
	for (int i = 0; i <= last; i++) {
		assert(olist->len < olist->size);

		struct object *obj = equip ? slot_object(player, i) : objects[i];
		struct object_menu_item *item = &olist->items[olist->len];
		char key = 0;

		if (object_test(tester, obj) || SHOW_OBJECT_IF_GOLD(obj)) {
			/* Acceptable items get a label */
			key = *keys++;
			assert(key != 0);

			/* item menu uses its own tags, so these labels are only
			 * for inven/equip subwindows and for the death screen */
			if (!menu) {
				item->label.len =
					strnfmt(item->label.str, item->label.size, "%c) ", key);
			}
		} else if (window || SHOW_OBJECT_IF_EMPTY(obj)) {
			/* Unacceptable items are still sometimes shown
			 * (pretty much only in equip subwindow/menu) */
			key = 0;

			if (!menu) {
				item->label.len =
					my_strcpy(item->label.str, "   ", item->label.size);
			}
		} else {
			continue;
		}

		/* Show full slot labels for equipment or quiver */
		if (equip && !terse) {
			item->equip.len = 
				strnfmt(item->equip.str, item->equip.size,
						"%-14s ", equip_mention(player, i));
			my_strcap(item->equip.str);
		} else if (quiver && !terse) {
			item->equip.len =
				strnfmt(item->equip.str, item->equip.size,
						"Slot %-9d ", i);
		} else {
			item->equip.len = 0;
		}

		item->object = obj;
		item->key = key;

		olist->len++;
	}

#undef SHOW_OBJECT_IF_GOLD
#undef SHOW_OBJECT_IF_EMPTY

	/* Set the names and get the max length */
	set_olist_names(olist, terse);
	set_olist_extra(olist, mode);
}

static int quiver_slots(int stack)
{
	return (player->upkeep->quiver_cnt + stack - 1) / stack;
}

/* Returns coords of the next row (after the ones shown) */
static struct loc show_quiver_compact(const char *keys, int height, struct loc loc)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	int stack = z_info->stack_size;
	int slots = quiver_slots(stack);
	int last_slot = slots - 1;

	uint32_t attr = menu_row_style(false, false);

	/* Quiver may take multiple lines */
	for (int slot = 0; slot < slots && loc.y < height; slot++) {
		char key = *keys++;
		assert(key != 0);

		if (slot == last_slot) {
			/* Last slot has less than the full number of missiles */
			stack = player->upkeep->quiver_cnt - (stack * last_slot);
		}

		erase_line(loc);

		/* Print the (disabled) label */
		strnfmt(buf, sizeof(buf), "%c) ", key);
		c_put_str(attr, buf, loc);
		loc.x += 3;

		/* Print the count */
		strnfmt(buf, sizeof(buf), "in Quiver: %d missile%s", stack, stack == 1 ? "" : "s");
		c_put_str(COLOUR_L_UMBER, buf, loc);
		loc.x -= 3;

		loc.y++;
	}

	return loc;
}

/**
 * Display a list of objects. Each object may be prefixed with a label.
 * Used by show_inven(), show_equip(), and show_floor(). Mode flags are
 * documented in object.h. Returns coordinates of the next row (after
 * the ones that were printed)
 */
static struct loc show_obj_list(struct object_menu_list *olist,
		int mode, int height, struct loc loc)
{
	for (size_t i = 0; i < olist->len && loc.y < height; i++, loc.y++)
	{
		show_item(&olist->items[i],
				loc, olist->extra_fields_offset, false, mode);
	}

	if (mode & OLIST_QUIVER_COMPACT) {
		loc = show_quiver_compact(all_letters + olist->len, height, loc);
	}

	return loc;
}

/**
 * Display the inventory. Builds a list of objects and passes them
 * off to show_obj_list() for display. Mode flags documented in
 * object.h
 */
void show_inven(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	struct loc loc = {0, 0};
	int height = Term_height();

	if (mode & OLIST_WINDOW) {
		/* Inven subwindow starts with a burden header */
		char buf[ANGBAND_TERM_STANDARD_WIDTH];
		int diff = weight_remaining(player);

		strnfmt(buf, sizeof(buf),
				"Burden %d.%d lb (%d.%d lb %s) ",
				player->upkeep->total_weight / 10,
				player->upkeep->total_weight % 10,
				abs(diff) / 10,
				abs(diff) % 10,
				(diff < 0 ? "overweight" : "remaining"));

		prt(buf, loc);
		loc.y++;
	}

	int last = -1;
	/* Find the last occupied inventory slot */
	for (int i = 0; i < z_info->pack_size; i++) {
		if (player->upkeep->inven[i] != NULL) {
			last = i;
		}
	}

	if (mode & OLIST_WINDOW) {
		/* Show compact names of objects in inven subwindow */
		mode |= OLIST_TERSE;
	}

	build_obj_list(olist, player->upkeep->inven, last,
			all_letters, tester, mode);
	show_obj_list(olist, mode, height, loc);
	free_obj_list(olist);

	Term_flush_output();
}

/**
 * Display the quiver. Builds a list of objects and passes them
 * off to show_obj_list() for display. Mode flags documented in
 * object.h
 */
void show_quiver(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	struct loc loc = {0, 0};
	int height = Term_height();

	/* Find the last occupied quiver slot */
	int last = -1;
	for (int i = 0; i < z_info->quiver_size; i++) {
		if (player->upkeep->quiver[i] != NULL) {
			last = i;
		}
	}

	mode |= OLIST_QUIVER_FULL; 

	build_obj_list(olist, player->upkeep->quiver, last,
			all_digits, tester, mode);
	show_obj_list(olist, mode, height, loc);
	free_obj_list(olist);

	Term_flush_output();
}

/**
 * Display the equipment. Builds a list of objects and passes them
 * off to show_obj_list() for display. Mode flags documented in
 * object.h
 */
void show_equip(int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	struct loc loc = {0, 0};
	int height = Term_height();

	size_t line_max_len = 0;

	if (mode & OLIST_WINDOW) {
		/* Show compact names of objects in equip subwindow */
		mode |= OLIST_TERSE;
	}

	build_obj_list(olist, NULL, player->body.count - 1,
			all_letters, tester, mode);
	line_max_len = olist->line_max_len;
	loc = show_obj_list(olist, mode, height, loc);
	free_obj_list(olist);

	/* Show the quiver in equip subwindow, if there is enough space */
	if ((mode & OLIST_WINDOW) && loc.y < height) {
		prt("In quiver", loc);
		loc.y++;

		olist = get_obj_list();

		int last = -1;
		for (int i = 0; i < z_info->quiver_size; i++) {
			if (player->upkeep->quiver[i] != NULL) {
				last = i;
			}
		}

		/* Linu up weight display of equip and quiver */
		olist->line_max_len = line_max_len;

		build_obj_list(olist, player->upkeep->quiver, last,
				all_digits, tester, mode);
		show_obj_list(olist, mode, height, loc);
		free_obj_list(olist);
	}

	Term_flush_output();
}

/**
 * Display the floor. Builds a list of objects and passes them
 * off to show_obj_list() for display. Mode flags documented in
 * object.h
 */
void show_floor(struct object **floor_list, int floor_num,
		int mode, item_tester tester)
{
	struct object_menu_list *olist = get_obj_list();

	struct loc loc = {0, 0};
	int height = Term_height();

	if (floor_num > z_info->floor_size) {
		floor_num = z_info->floor_size;
	}

	build_obj_list(olist, floor_list, floor_num - 1,
			all_letters, tester, mode);
	show_obj_list(olist, mode, height, loc);
	free_obj_list(olist);

	Term_flush_output();
}

/**
 * ------------------------------------------------------------------------
 * Variables for object selection
 * ------------------------------------------------------------------------
 */

struct object_menu_data {
	struct object_menu_list *list;

	struct object **floor_list;

	/* Return values of menu */
	struct {
		struct object *object;
		bool new_menu;
	} retval;

	int i1, i2; /* first and last inven object */
	int e1, e2; /* first and last equip object */
	int q1, q2; /* first and last quiver object */
	int f1, f2; /* first and last floor object */

	int olist_mode;
	int item_mode;

	cmd_code item_cmd;
};

/**
 * ------------------------------------------------------------------------
 * Object selection utilities
 * ------------------------------------------------------------------------
 */

/**
 * Prevent certain choices depending on the inscriptions on the item.
 */
bool get_item_allow(const struct object *obj, cmd_code cmd, bool harmless)
{
	if (obj->note == 0) {
		return true;
	}

	unsigned char ch = cmd_lookup_key(cmd, KEYMAP_MODE_OPT);
	if (ch < 0x20) {
		ch = UN_KTRL(ch);
	}

	char inscrip[] = {'!', ch, 0};

	unsigned checks = check_for_inscrip(obj, inscrip);
	if (!harmless) {
		checks += check_for_inscrip(obj, "!*");
	}

	if (checks > 0) {
		char prompt[ANGBAND_TERM_STANDARD_WIDTH];
		const unsigned n_checks = checks;

		const char *verb = cmd_verb(cmd);
		if (verb == NULL) {
			verb = "do that with";
		}

		while (checks > 0) {
			strnfmt(prompt, sizeof(prompt),
					"(%u/%u) Really %s", checks, n_checks, verb);

			if (verify_object(prompt, (struct object *) obj)) {
				checks--;
			} else {
				return false;
			}
		}
	}

	return true;
}

/**
 * Find the index of the first object in the object list with the given "tag".
 *
 * A tag is a char "n" appearing as "@n" anywhere in the inscription of an object.
 * Also, the tag "@xn" will work as well, where "n" is a tag-char,
 * and "x" is the command that tag will work for.
 */
static bool find_inscribed_object(struct object_menu_list *olist,
		int *index, char inscrip, cmd_code cmd, bool check_quiver)
{
	const int mode = KEYMAP_MODE_OPT;

	/* (f)ire is handled differently from all others, due to the quiver */
	if (check_quiver) {
		int i = D2I(inscrip);

		if (i >= 0 && i < z_info->quiver_size
				&& player->upkeep->quiver[i])
		{
			*index = i;
			return true;
		}
	}

	unsigned char cmdkey = cmd_lookup_key(cmd, mode);
	if (cmdkey < 0x20) {
		cmdkey = UN_KTRL(cmdkey);
	}

	/* Check every object in the object list */
	for (size_t i = 0; i < olist->len; i++) {
		struct object *obj = olist->items[i].object;

		if (obj != NULL && obj->note != 0) {
			for (const char *s = strchr(quark_str(obj->note), '@');
					s != NULL;
					s = strchr(s + 1, '@'))
			{
				if (s[1] == inscrip || (s[1] == (char) cmdkey && s[2] == inscrip)) {
					*index = i;
					return true;
				}
			}
		}
	}

	return false;
}

/**
 * ------------------------------------------------------------------------
 * Object selection menu
 * ------------------------------------------------------------------------
 */

static void cat_menu_hint(char *buf, size_t bufsize,
		bool tags, int tag_from, int tag_to,
		bool inven, bool equip, bool quiver, bool floor)
{
	if (tags) {
		char tmp_buf[ANGBAND_TERM_STANDARD_WIDTH];

		strnfmt(tmp_buf, sizeof(tmp_buf), "%c-%c, ", I2A(tag_from), I2A(tag_to));
		my_strcat(buf, tmp_buf, bufsize);
	}

	/* Only one of those is allowed, and inventory takes precedence */
	if (inven) {
		my_strcat(buf, "/ for Inven, ", bufsize);
	} else if (equip) {
		my_strcat(buf, "/ for Equip, ", bufsize);
	}

	if (quiver) {
		my_strcat(buf, "| for Quiver, ", bufsize);
	}

	if (floor) {
		my_strcat(buf, "- for floor, ", bufsize);
	}
}

/**
 * Make the correct header for the selection menu
 */
static void menu_hint(const struct object_menu_data *data,
		char *hint, size_t hint_size)
{
	const int i1 = data->i1;
	const int i2 = data->i2;
	const int e1 = data->e1;
	const int e2 = data->e2;
	const int q1 = data->q1;
	const int q2 = data->q2;
	const int f1 = data->f1;
	const int f2 = data->f2;

	const bool use_inven  = (data->item_mode & USE_INVEN)  ? true : false;
	const bool use_equip  = (data->item_mode & USE_EQUIP)  ? true : false;
	const bool use_quiver = (data->item_mode & USE_QUIVER) ? true : false;
	const bool use_floor  = (f1 <= f2)                     ? true : false;

	my_strcpy(hint, "(", hint_size);

	if (player->upkeep->command_wrk == USE_INVEN) {
		cat_menu_hint(hint, hint_size,
				i1 <= i2, i1, i2,
				false, use_equip, use_quiver, use_floor);

	} else if (player->upkeep->command_wrk == USE_EQUIP) {
		cat_menu_hint(hint, hint_size,
				e1 <= e2, e1, e2,
				use_inven, false, use_quiver, use_floor);

	} else if (player->upkeep->command_wrk == USE_QUIVER) {
		cat_menu_hint(hint, hint_size,
				q1 <= q2, q1, q2,
				use_inven, use_equip, false, use_floor);

	} else {
		cat_menu_hint(hint, hint_size,
				f1 <= f2, f1, f2,
				use_inven, use_equip, use_quiver, false);
	}

	my_strcat(hint, "ESC)", hint_size);
}

static bool handle_menu_key_action(struct object_menu_data *data,
		struct keypress key)
{
	const bool inven  = (data->item_mode & USE_INVEN)  ? true : false;
	const bool equip  = (data->item_mode & USE_EQUIP)  ? true : false;
	const bool quiver = (data->item_mode & USE_QUIVER) ? true : false;
	const bool floor  = (data->item_mode & USE_FLOOR)  ? true : false;

	switch (key.code) {
		case '/':
			if (inven && player->upkeep->command_wrk != USE_INVEN) {
				player->upkeep->command_wrk = USE_INVEN;
				data->retval.new_menu = true;
			} else if (equip && player->upkeep->command_wrk != USE_EQUIP) {
				player->upkeep->command_wrk = USE_EQUIP;
				data->retval.new_menu = true;
			} else {
				bell("Cannot switch item selector!");
			}
			break;

		case '|':
			if (quiver) {
				player->upkeep->command_wrk = USE_QUIVER;
				data->retval.new_menu = true;
			} else {
				bell("Cannot select quiver!");
			}
			break;

		case '-':
			if (floor) {
				player->upkeep->command_wrk = USE_FLOOR;
				data->retval.new_menu = true;
			} else {
				bell("Cannot select floor!");
			}
			break;

		default:
			bell("bad selector '%u' in item menu!", (unsigned) key.code);
			break;
	}

	/* false stops current menu */
	return data->retval.new_menu ? false : true;
}

static bool handle_menu_select_action(struct object_menu_data *data,
		int index)
{
	struct object *obj = data->list->items[index].object;

	if (obj != NULL
			&& get_item_allow(obj,
				data->item_cmd,
				(data->item_mode & IS_HARMLESS)))
	{
		data->retval.object = obj;
		/* Stop the menu */
		return false;
	} else {
		/* Let the menu continue to work */
		return true;
	}
}

/**
 * Get an item tag
 */
static char get_item_tag(struct menu *menu, int index)
{
	struct object_menu_data *data = menu_priv(menu);

	return data->list->items[index].key;
}

static bool get_item_validity(struct menu *menu, int index)
{
	struct object_menu_data *data = menu_priv(menu);

	return data->list->items[index].object != NULL;
}

/**
 * Display an entry on the item menu
 */
static void get_item_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	(void) width;

	struct object_menu_data *data = menu_priv(menu);

	show_item(&data->list->items[index],
			loc, data->list->extra_fields_offset, cursor, data->olist_mode);
}

/**
 * Deal with events on the get_item menu
 */
static bool get_item_action(struct menu *menu, const ui_event *event, int index)
{
	struct object_menu_data *data = menu_priv(menu);

	if (event->type == EVT_SELECT) {
		return handle_menu_select_action(data, index);
	} else if (event->type == EVT_KBRD) {
		return handle_menu_key_action(data, event->key);
	} else {
		return false;
	}
}

/**
 * Show quiver missiles in full inventory
 */
static void quiver_browser(int index, void *menu_data, region active)
{
	(void) index;

	struct object_menu_data *data = menu_data;

	if ((data->olist_mode & OLIST_QUIVER_COMPACT)
			&& player->upkeep->command_wrk == USE_INVEN)
	{
		struct loc loc = {
			.x = active.x,
			.y = active.y + active.h - 1
		};

		/* The term is unlikely to be 999 rows high;
		 * but it should be high enough to show all missiles in quiver */
		show_quiver_compact(all_letters + data->list->len, 999, loc);
	}
}

static void change_command_wrk(const struct object_menu_data *data, ui_event event)
{
	assert(event.key.code == ARROW_LEFT || event.key.code == ARROW_RIGHT);

	static const int wrk_order[] = {
		USE_EQUIP,
		USE_INVEN,
		USE_QUIVER,
		USE_FLOOR,
	};

	const size_t n_wrks = N_ELEMENTS(wrk_order);

	size_t i = 0;
	while (i < n_wrks && player->upkeep->command_wrk != wrk_order[i]) {
		i++;
	}
	assert(i < n_wrks);

	for (size_t n = 0; n < n_wrks; n++) {
		if (event.key.code == ARROW_LEFT) {
			/* Unsigned wrap around */
			i = MIN(i - 1, n_wrks - 1);
		} else if (event.key.code == ARROW_RIGHT) {
			i = (i + 1) % n_wrks;
		}

		if (wrk_order[i] & data->item_mode) {
			player->upkeep->command_wrk = wrk_order[i];
			return;
		}
	}
}

static void menu_find_inscriptions(struct menu *menu,
		char *inscriptions, size_t size)
{
	assert(size >= 10);

	struct object_menu_data *data = menu_priv(menu);

	const bool check_quiver = (data->item_mode & QUIVER_TAGS) ? true : false;

	for (int i = 0; i < 10; i++) {
		int index = 0;
		if (find_inscribed_object(data->list,
					&index, I2D(i), data->item_cmd, check_quiver))
		{
			inscriptions[i] = data->list->items[index].key;
		}
	}
}

/**
 * Display list items to choose from
 */
static void item_menu(struct object_menu_data *data, const char *title)
{
	menu_iter iter = {
		.get_tag     = get_item_tag,
		.valid_row   = get_item_validity,
		.display_row = get_item_display,
		.row_handler = get_item_action
	};

	struct menu menu;
	menu_init(&menu, MN_SKIN_OBJECT, &iter);

	menu_setpriv(&menu, data->list->len, data);

	menu.selections =
		player->upkeep->command_wrk == USE_QUIVER ? all_digits : lower_case;

	menu.stop_keys = "/|-";
	menu.browse_hook = quiver_browser;

	mnflag_on(menu.flags, MN_PVT_TAGS);
	mnflag_on(menu.flags, MN_INSCRIP_TAGS);
	mnflag_on(menu.flags, MN_DONT_CLEAR);

	char inscriptions[10] = {0};
	menu_find_inscriptions(&menu, inscriptions, sizeof(inscriptions));
	menu.inscriptions = inscriptions;

	menu.title = title;

	region reg = {
		.x = 0,
		.y = 0,
		.w = 0, /* full term width */
		.h = data->list->len + 3
	};
	menu_layout(&menu, reg);

	ui_event event = menu_select(&menu);

	if (event.type == EVT_SWITCH) {
		change_command_wrk(data, event);
		data->retval.new_menu = true;
	}
}

static void show_menu_prompt(const struct object_menu_data *data,
		const char *prompt)
{
	char hint[ANGBAND_TERM_STANDARD_WIDTH];
	menu_hint(data, hint, sizeof(hint));

	char menu_prompt[ANGBAND_TERM_STANDARD_WIDTH];
	my_strcpy(menu_prompt, prompt, sizeof(menu_prompt));
	my_strcat(menu_prompt, " ",    sizeof(menu_prompt));
	my_strcat(menu_prompt, hint,   sizeof(menu_prompt));

	show_prompt(menu_prompt, false);
}

static void build_menu_list(struct object_menu_data *data,
		item_tester tester, const char **title)
{
	if (data->list != NULL) {
		free_obj_list(data->list);
	}

	data->list = get_obj_list();

	if (player->upkeep->command_wrk == USE_INVEN) {
		if (data->i1 <= data->i2) {
			build_obj_list(data->list, player->upkeep->inven, data->i2,
					lower_case, tester, data->olist_mode);
		}
		*title = "Inventory";
	} else if (player->upkeep->command_wrk == USE_EQUIP) {
		if (data->e1 <= data->e2) {
			build_obj_list(data->list, NULL, data->e2,
					lower_case, tester, data->olist_mode);
		}
		*title = "Equipment";
	} else if (player->upkeep->command_wrk == USE_QUIVER) {
		if (data->q1 <= data->q2) {
			build_obj_list(data->list, player->upkeep->quiver, data->q2,
					all_digits, tester, data->olist_mode);
		}
		*title = "Quiver";
	} else if (player->upkeep->command_wrk == USE_FLOOR) {
		if (data->f1 <= data->f2) {
			build_obj_list(data->list, data->floor_list, data->f2,
					lower_case, tester, data->olist_mode);
		}
		*title = "Floor";
	} else {
		/* Should never happen */
		quit_fmt("Bad command_wrk %d in menu!", player->upkeep->command_wrk);
	}
}

static bool init_menu_data(struct object_menu_data *data,
		cmd_code cmd, item_tester tester, int mode)
{
	const bool use_inven  = (mode & USE_INVEN)  ? true : false;
	const bool use_equip  = (mode & USE_EQUIP)  ? true : false;
	const bool use_quiver = (mode & USE_QUIVER) ? true : false;
	const bool use_floor  = (mode & USE_FLOOR)  ? true : false;

	memset(data, 0, sizeof(*data));

	data->retval.object = NULL;
	data->list          = NULL;

	data->floor_list = mem_zalloc(z_info->floor_size * sizeof(*data->floor_list));
	data->item_cmd   = cmd;
	data->item_mode  = mode;

	data->olist_mode |= OLIST_MENU;

	/* Object list display modes */
	if (mode & SHOW_FAIL) {
		data->olist_mode |= OLIST_FAIL;
	} else {
		data->olist_mode |= OLIST_WEIGHT;
	}
	if (mode & SHOW_PRICES) {
		data->olist_mode |= OLIST_PRICE;
	}
	if (mode & SHOW_EMPTY) {
		data->olist_mode |= OLIST_SHOW_EMPTY;
	}
	if (mode & SHOW_QUIVER) {
		data->olist_mode |= OLIST_QUIVER_COMPACT;
	}

	int i1 = 0;
	int i2 = use_inven ? z_info->pack_size - 1 : -1;
	while (i1 <= i2 && !object_test(tester, player->upkeep->inven[i1])) {
		i1++;
	}
	while (i1 <= i2 && !object_test(tester, player->upkeep->inven[i2])) {
		i2--;
	}
	if (i1 > i2) {
		data->item_mode &= ~USE_INVEN;
	}

	int e1 = 0;
	int e2 = use_equip ? player->body.count - 1 : -1;
	if (data->item_cmd != CMD_NULL || tester != NULL) {
		while (e1 <= e2 && !object_test(tester, slot_object(player, e1))) {
			e1++;
		}
		while (e1 <= e2 && !object_test(tester, slot_object(player, e2))) {
			e2--;
		}
	} else if (use_equip) {
		/* check if there is at least one valid object in equip */
		e2 = -1;
		for (int i = 0; i < player->body.count; i++) {
			if (slot_object(player, i) != NULL) {
				e2 = player->body.count - 1;
				break;
			}
		}
	}
	if (e1 > e2) {
		data->item_mode &= ~USE_EQUIP;
	}

	int q1 = 0;
	int q2 = use_quiver ? z_info->quiver_size - 1 : -1;
	while (q1 <= q2 && !object_test(tester, player->upkeep->quiver[q1])) {
		q1++;
	}
	while (q1 <= q2 && !object_test(tester, player->upkeep->quiver[q2])) {
		q2--;
	}
	if (q1 > q2) {
		data->item_mode &= ~USE_QUIVER;
	}

	int floor_num = use_floor ?
		scan_floor(data->floor_list, z_info->floor_size,
				OFLOOR_TEST | OFLOOR_SENSE | OFLOOR_VISIBLE, tester) : 0;
	int f1 = 0;
	int f2 = floor_num - 1;
	while (f1 <= f2 && !object_test(tester, data->floor_list[f1])) {
		f1++;
	}
	while (f1 <= f2 && !object_test(tester, data->floor_list[f2])) {
		f2--;
	}
	if (f1 > f2) {
		data->item_mode &= ~USE_FLOOR;
	}

	data->i1 = i1;
	data->i2 = i2;

	data->e1 = e1;
	data->e2 = e2;

	data->q1 = q1;
	data->q2 = q2;

	data->f1 = f1;
	data->f2 = f2;

	const bool allow_inven  = (data->item_mode & USE_INVEN)   ? true : false;
	const bool allow_equip  = (data->item_mode & USE_EQUIP)   ? true : false;
	const bool allow_quiver = (data->item_mode & USE_QUIVER)  ? true : false;
	const bool allow_floor  = (data->item_mode & USE_FLOOR)   ? true : false;

	/* Require at least one legal choice */
	if (!allow_inven && !allow_equip && !allow_quiver && !allow_floor) {
		return false;
	}

	/* Start where requested if possible */
	if (player->upkeep->command_wrk == USE_EQUIP && allow_equip) {
		player->upkeep->command_wrk = USE_EQUIP;
	} else if (player->upkeep->command_wrk == USE_INVEN && allow_inven) {
		player->upkeep->command_wrk = USE_INVEN;
	} else if (player->upkeep->command_wrk == USE_QUIVER && allow_quiver) {
		player->upkeep->command_wrk = USE_QUIVER;
	} else if (player->upkeep->command_wrk == USE_FLOOR && allow_floor) {
		player->upkeep->command_wrk = USE_FLOOR;
	} else if ((data->item_mode & QUIVER_TAGS) && allow_quiver) {
		/* If we are obviously using the quiver then start on quiver */
		player->upkeep->command_wrk = USE_QUIVER;
	} else {
		/* Otherwise choose whatever is allowed */
		if (use_inven && allow_inven) {
			player->upkeep->command_wrk = USE_INVEN;
		} else if (use_equip && allow_equip) {
			player->upkeep->command_wrk = USE_EQUIP;
		} else if (use_quiver && allow_quiver) {
			player->upkeep->command_wrk = USE_QUIVER;
		} else if (use_floor && allow_floor) {
			player->upkeep->command_wrk = USE_FLOOR;
		} else {
			/* If nothing to choose, use inventory
			 * (that should never happen?) */
			player->upkeep->command_wrk = USE_INVEN;
		}
	}

	return true;
}

static void cleanup_menu_data(struct object_menu_data *data)
{
	mem_free(data->floor_list);

	if (data->list != NULL) {
		free_obj_list(data->list);
	}
}

static void push_item_term(const struct object_menu_data *data)
{
	/* Don't show completely empty quiver */
	if (player->upkeep->command_wrk == USE_QUIVER
			&& quiver_slots(z_info->stack_size) == 0)
	{
		data->list->len = 0;
	}

	/* Handle empty floor, inventory, quiver */
	bool empty = data->list->len > 0 ? false : true;

	/* We add 3 to term's width to account for the menu's tags;
	 * then add 3 to term's width and height to reserve space
	 * for padding on the left, right, top and bottom sides
	 * of the item menu (see menu_layout() and note that
	 * our menu has a title);
	 *
	 * Minimum width 30 for empty list is arbitrary.
	 *
	 * Minimum height of the term is 3; one row for the title,
	 * one empty row, one row for menu items (even if there
	 * are no menu items - it's still required). */

	struct term_hints hints = {
		.width = empty ? 30 : data->list->total_max_len + 3 + 3,
		.height = empty ? 3 : data->list->len + 3,
		.purpose = TERM_PURPOSE_MENU,
		.position = TERM_POSITION_TOP_CENTER
	};

	if (player->upkeep->command_wrk == USE_INVEN
			&& (data->olist_mode & OLIST_QUIVER_COMPACT))
	{
		/* Add space for quiver */
		hints.height += quiver_slots(z_info->stack_size);
	}

	Term_push_new(&hints);
}

static void pop_item_term(void)
{
	Term_pop();
}

/**
 * Let the user select an object, save its address
 *
 * Return true only if an acceptable item was chosen by the user.
 *
 * The user is allowed to choose acceptable items from the equipment,
 * inventory, quiver, or floor, respectively, if the proper flag was given,
 * and there are any acceptable items in that location.
 *
 * If there are no acceptable items available anywhere, and "str" is
 * not NULL, then it will be used as the text of a warning message
 * before the function returns.
 *
 * If a legal item is selected , we save it in "choice" and return true.
 *
 * If no item is available, we store NULL in "choice", and we display a
 * warning message, using "reject" if available, and return false.
 *
 * If no item is selected, we store NULL in "choice", and return false.
 *
 * Global "player->upkeep->command_wrk" is used to choose between
 * equip/inven/quiver/floor listings. It is equal to USE_INVEN or USE_EQUIP or
 * USE_QUIVER or USE_FLOOR, except when this function is first called, when it
 * is equal to zero, which will cause it to be set to USE_INVEN.
 *
 * We always erase the prompt when we are done, leaving a blank line,
 * or a warning message, if appropriate, if no items are available.
 *
 * Note that only "acceptable" floor objects get indexes, so between two
 * commands, the indexes of floor objects may change. XXX XXX XXX
 */
bool textui_get_item(struct object **choice,
		const char *prompt, const char *reject,
		cmd_code cmd, item_tester tester, int mode)
{
	struct object_menu_data data;

	bool menu_ok = init_menu_data(&data, cmd, tester, mode);

	const char *menu_title = NULL;

	if (menu_ok) {
		do {
			build_menu_list(&data, tester, &menu_title);

			push_item_term(&data);

			data.retval.new_menu = false;
			data.retval.object = NULL;

			if (prompt) {
				show_menu_prompt(&data, prompt);
			}

			item_menu(&data, menu_title);
			clear_prompt();

			pop_item_term();
		} while (data.retval.new_menu);

	} else if (reject) {
		msg("%s", reject);
	}

	*choice = data.retval.object;

	cleanup_menu_data(&data);

	return *choice != NULL;
}

/**
 * ------------------------------------------------------------------------
 * Object recall
 * ------------------------------------------------------------------------
 */

/*
 * This function is called internally by object recall display functions
 */
static void display_object_recall_int(const struct object *obj,
		int mode, bool interactive)
{
	textblock *tb = object_info(obj, OINFO_NONE);

	char header[ANGBAND_TERM_STANDARD_WIDTH];
	object_desc(header, sizeof(header), obj, mode);

	region area = {0, 0, 0, 0};

	if (interactive) {
		textui_textblock_show(tb, area, header);
	} else {
		Term_clear();
		textui_textblock_place(tb, area, header);
	}

	textblock_free(tb);
}

/**
 * This draws the object recall subwindow when displaying a particular object
 * (e.g. a helmet in the backpack, or a scroll on the ground)
 */
void display_object_recall(struct object *obj)
{
	display_object_recall_int(obj, ODESC_PREFIX | ODESC_FULL, false);
}

/**
 * This draws the object recall subwindow when displaying a recalled item kind
 * (e.g. a generic ring of acid or a generic blade of chaos)
 */
void display_object_kind_recall(struct object_kind *kind)
{
	struct object object = OBJECT_NULL;
	struct object known_obj = OBJECT_NULL;

	object_prep(&object, kind, 0, EXTREMIFY);
	object.known = &known_obj;

	display_object_recall_int(&object, ODESC_PREFIX | ODESC_FULL, false);
}

/**
 * Display object recall modally and wait for a keypress.
 * This is set up for use in look mode (see target_set_interactive_aux()).
 * \param obj is the object to be described.
 */
void display_object_recall_interactive(struct object *obj)
{
	display_object_recall_int(obj, ODESC_PREFIX | ODESC_FULL, true);
}

/**
 * Examine an object
 */
void textui_obj_examine(void)
{
	struct object *obj;

	if (get_item(&obj, "Examine which item? ", "You have nothing to examine.",
			CMD_NULL, NULL, (USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | IS_HARMLESS)))
	{
		/* Track object for object recall */
		track_object(player->upkeep, obj);

		display_object_recall_int(obj,
				ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL, true);
	}
}

/**
 * ------------------------------------------------------------------------
 * Object ignore interface
 * ------------------------------------------------------------------------
 */

enum {
	IGNORE_THIS_ITEM,
	UNIGNORE_THIS_ITEM,
	IGNORE_THIS_FLAVOR,
	UNIGNORE_THIS_FLAVOR,
	IGNORE_THIS_EGO,
	UNIGNORE_THIS_EGO,
	IGNORE_THIS_QUALITY
};

static void ignore_menu_build(struct menu *menu, struct object *obj)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];

	/* Basic ignore option */
	if (obj->known->notice & OBJ_NOTICE_IGNORE) {
		menu_dynamic_add(menu, "Unignore this item", UNIGNORE_THIS_ITEM);
	} else {
		menu_dynamic_add(menu, "This item only", IGNORE_THIS_ITEM);
	}

	/* Flavour-aware ignore */
	if (ignore_tval(obj->tval)
			&& (!obj->artifact || !object_flavor_is_aware(obj)))
	{
		char name[ANGBAND_TERM_STANDARD_WIDTH];
		object_desc(name, sizeof(name), obj,
					ODESC_NOEGO | ODESC_BASE | ODESC_PLURAL);

		if (kind_is_ignored_aware(obj->kind)
				|| kind_is_ignored_unaware(obj->kind))
		{
			strnfmt(buf, sizeof(buf), "Unignore all %s", name);
			menu_dynamic_add(menu, buf, UNIGNORE_THIS_FLAVOR);
		} else {
			strnfmt(buf, sizeof(buf), "All %s", name);
			menu_dynamic_add(menu, buf, IGNORE_THIS_FLAVOR);
		}
	}

	/* Ego ignoring */
	if (obj->known->ego) {
		struct ego_desc choice = {
			.e_idx = obj->ego->eidx,
			.itype = ignore_type_of(obj),
			.short_name = ""
		};

		char name[ANGBAND_TERM_STANDARD_WIDTH];
		ego_item_name(name, sizeof(name), &choice);

		if (ego_is_ignored(choice.e_idx, choice.itype)) {
			strnfmt(buf, sizeof(buf), "Unignore all %s", name + 4);
			menu_dynamic_add(menu, buf, UNIGNORE_THIS_EGO);
		} else {
			strnfmt(buf, sizeof(buf), "All %s", name + 4);
			menu_dynamic_add(menu, buf, IGNORE_THIS_EGO);
		}
	}

	/* Quality ignoring */
	byte ignore_value = ignore_level_of(obj);
	int ignore_type = ignore_type_of(obj);

	if ((!tval_is_jewelry(obj) || ignore_value == IGNORE_BAD)
			&& ignore_value < IGNORE_MAX
			&& ignore_type  < ITYPE_MAX)
	{
		strnfmt(buf, sizeof(buf), "All %s %s",
				quality_values[ignore_value].name, ignore_name_for_type(ignore_type));
		menu_dynamic_add(menu, buf, IGNORE_THIS_QUALITY);
	}
}

void textui_cmd_ignore_menu(struct object *obj)
{
	if (obj == NULL) {
		return;
	}


	struct menu *menu = menu_dynamic_new();

	menu->selections = lower_case;
	ignore_menu_build(menu, obj);

	/* Work out display region */
	region reg = menu_dynamic_calc_location(menu);
	struct term_hints hints = {
		.width = reg.w + 2,
		.height = reg.h + 2,
		.position = TERM_POSITION_TOP_CENTER,
		.purpose = TERM_PURPOSE_MENU
	};

	Term_push_new(&hints);

	reg.x = 1;
	reg.y = 1;
	menu_layout(menu, reg);

	show_prompt("(Enter to select, ESC) Ignore:", false);
	int selected = menu_dynamic_select(menu);

	menu_dynamic_free(menu);
	Term_pop();

	if (selected == IGNORE_THIS_ITEM) {
		obj->known->notice |= OBJ_NOTICE_IGNORE;
	} else if (selected == UNIGNORE_THIS_ITEM) {
		obj->known->notice &= ~OBJ_NOTICE_IGNORE;
	} else if (selected == IGNORE_THIS_FLAVOR) {
		object_ignore_flavor_of(obj);
	} else if (selected == UNIGNORE_THIS_FLAVOR) {
		kind_ignore_clear(obj->kind);
	} else if (selected == IGNORE_THIS_EGO) {
		ego_ignore(obj);
	} else if (selected == UNIGNORE_THIS_EGO) {
		ego_ignore_clear(obj);
	} else if (selected == IGNORE_THIS_QUALITY) {
		int ignore_type = ignore_type_of(obj);
		byte ignore_value = ignore_level_of(obj);

		ignore_level[ignore_type] = ignore_value;
	}

	player->upkeep->notice |= PN_IGNORE;
}

void textui_cmd_ignore(void)
{
	struct object *obj;

	if (get_item(&obj, "Ignore which item? ", "You have nothing to ignore.",
				CMD_IGNORE, NULL,
				USE_INVEN | USE_QUIVER | USE_EQUIP | USE_FLOOR))
	{
		textui_cmd_ignore_menu(obj);
	}
}

void textui_cmd_toggle_ignore(void)
{
	player->unignoring = !player->unignoring;
	player->upkeep->notice |= PN_IGNORE;
	do_cmd_redraw();
}

/**
 * ------------------------------------------------------------------------
 * Display of individual objects in lists or for selection
 * ------------------------------------------------------------------------
*/

/**
 * Determine if the attr and char should consider the item's flavor.
 * Identified scrolls should use their own tile.
 */
static bool use_flavor_glyph(const struct object_kind *kind)
{
	if (kind->tval == TV_SCROLL && kind->aware) {
		return false;
	} else {
		return kind->flavor != NULL;
	}
}

/**
 * Return the attr for a given item kind. Use flavor if available.
 * Default to user definitions.
 */
uint32_t object_kind_attr(const struct object_kind *kind)
{
	return use_flavor_glyph(kind) ?
		flavor_x_attr[kind->flavor->fidx] : kind_x_attr[kind->kidx];
}

/**
 * Return the char for a given item kind. Use flavor if available.
 * Default to user definitions.
 */
wchar_t object_kind_char(const struct object_kind *kind)
{
	return use_flavor_glyph(kind) ?
		flavor_x_char[kind->flavor->fidx] : kind_x_char[kind->kidx];
}

/**
 * Return the attr for a given item. Use flavor if available.
 * Default to user definitions.
 */
uint32_t object_attr(const struct object *obj)
{
	return object_kind_attr(obj->kind);
}

/**
 * Return the char for a given item.
 * Use flavor if available.
 * Default to user definitions.
 */
wchar_t object_char(const struct object *obj)
{
	return object_kind_char(obj->kind);
}
