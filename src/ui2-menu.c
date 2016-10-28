/**
 * \file ui2-menu.c
 * \brief Generic menu interaction functions
 *
 * Copyright (c) 2007 Pete Mack
 * Copyright (c) 2010 Andi Sidwell
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
#include "angband.h"
#include "cave.h"
#include "ui2-target.h"
#include "ui2-event.h"
#include "ui2-input.h"
#include "ui2-menu.h"

/**
 * Some useful constants
 */
const char lower_case[]  = "abcdefghijklmnopqrstuvwxyz";
const char all_letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char all_digits[]  = "0123456789";

#define MENU_CURSOR_INVALID (-1)

/**
 * Forward declarations
 */
static void display_menu_row(struct menu *menu,
		int index, bool cursor, struct loc loc, int width);
static void display_menu_more(struct menu *menu, region reg, int row);
static bool is_valid_row(struct menu *menu, int cursor);
static bool has_valid_row(struct menu *menu);

/**
 * Get a menu selection char (tag) from index (menu row).
 */
static char menu_get_tag(const struct menu *menu, int index)
{
	int tag = 0;

	if (menu->selections != NULL) {
		int len;

		if (menu->selections == lower_case) {
			len = N_ELEMENTS(lower_case);
		} else if (menu->selections == all_letters) {
			len = N_ELEMENTS(all_letters);
		} else if (menu->selections == all_digits) {
			len = N_ELEMENTS(all_digits);
		} else {
			len = strlen(menu->selections);
		}

		if (index >= 0 && index < len) {
			tag = menu->selections[index];
		}
	}

	return tag;
}

/**
 * Select the color of menu's row,
 * based on whether it's a valid row
 * and whether it's currently under the cursor.
 */
uint32_t menu_row_style(bool valid, bool selected)
{
	static const uint32_t curs_attrs[2][2] = {
		{COLOUR_SLATE, COLOUR_BLUE},  /* Greyed row */
		{COLOUR_WHITE, COLOUR_L_BLUE} /* Valid row */
	};

	return curs_attrs[valid ? 1 : 0][selected ? 1 : 0];
}

/**
 * Helper functions dealing with menu's flags
 */
static bool menu_displays_tags(const struct menu *menu)
{
	return !mnflag_has(menu->flags, MN_NO_TAGS)
		&& !mnflag_has(menu->flags, MN_PVT_TAGS);
}

static bool menu_has_tags(const struct menu *menu)
{
	return !mnflag_has(menu->flags, MN_NO_TAGS);
}

static bool menu_has_inscription(const struct menu *menu, keycode_t code)
{
	if (mnflag_has(menu->flags, MN_INSCRIP_TAGS)) {
		assert(menu->inscriptions != NULL);

		return code < CHAR_MAX
			&& isdigit(code)
			&& menu->inscriptions[D2I(code)] != 0;
	} else {
		return false;
	}
}

static bool menu_is_caseless(const struct menu *menu)
{
	return mnflag_has(menu->flags, MN_CASELESS_TAGS);
}

static bool menu_displays_more(const struct menu *menu)
{
	return !mnflag_has(menu->flags, MN_NO_MORE);
}

static bool menu_needs_double_tap(const struct menu *menu)
{
	return mnflag_has(menu->flags, MN_DBL_TAP);
}

static bool menu_can_act(const struct menu *menu)
{
	return !mnflag_has(menu->flags, MN_NO_ACTION);
}

static bool menu_should_redraw(const struct menu *menu)
{
	return menu->old_cursor == MENU_CURSOR_INVALID;
}

void menu_force_redraw(struct menu *menu)
{
	menu->old_cursor = MENU_CURSOR_INVALID;
}

/**
 * Helper functions for managing menu's filter list
 */
static int menu_index(const struct menu *menu, int index)
{
	assert(index >= 0);

	if (menu->filter_list) {
		assert(index < menu->filter_count);
		return menu->filter_list[index];
	} else {
		return index;
	}
}

static int menu_count(const struct menu *menu)
{
	if (menu->filter_list) {
		return menu->filter_count;
	} else {
		return menu->count;
	}
}

/**
 * MN_ACTIONS helper functions
 *
 * MN_ACTIONS is the type of menu iterator that displays
 * a simple list of menu_actions.
 */

static char menu_action_tag(struct menu *menu, int index)
{
	menu_action *acts = menu_priv(menu);

	return acts[index].tag;
}

static bool menu_action_valid(struct menu *menu, int index)
{
	menu_action *acts = menu_priv(menu);

	return acts[index].name != NULL;
}

static void menu_action_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	menu_action *acts = menu_priv(menu);

	if (acts[index].name) {
		bool valid = (acts[index].flags & MN_ACT_GRAYED) ? false : true;

		Term_adds(loc.x, loc.y, width,
				menu_row_style(valid, cursor), acts[index].name);
	}
}

static bool menu_action_handle(struct menu *menu, const ui_event *event, int index)
{
	menu_action *acts = menu_priv(menu);

	if (event->type != EVT_SELECT
			|| acts[index].action == NULL)
	{
		return false;
	}

	if (!(acts[index].flags & MN_ACT_GRAYED)) {
		acts[index].action(acts[index].name, index);
	}

	return true;
}

/**
 * Virtual function table for action_events
 */
static const menu_iter menu_iter_actions = {
	.get_tag     = menu_action_tag,
	.valid_row   = menu_action_valid,
	.display_row = menu_action_display,
	.row_handler = menu_action_handle
};

/**
 * MN_STRINGS helper functions
 *
 * MN_STRINGS is the type of menu iterator that displays a simple list of 
 * strings - no action is associated, as selection will just return the index.
 */

static void display_string(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	const char **items = menu_priv(menu);

	Term_adds(loc.x, loc.y, width, menu_row_style(true, cursor), items[index]);
}

/**
 * Virtual function table for displaying arrays of strings
 */
static const menu_iter menu_iter_strings = { 
	.display_row = display_string,
};

/* ==================== SKINS ==================== */

/**
 * Find the position of a cursor given a subwindow address
 */
static int generic_skin_get_cursor(struct menu *menu,
		struct loc loc, int count, int top, region reg)
{
	(void) menu;

	int rely = loc.y - reg.y;

	if (rely < 0 || rely >= count) {
		return -1;
	} else {
		return rely + top;
	}
}

/**
 * Display current view of a skin
 */
static void generic_skin_display(struct menu *menu, int cursor, region reg)
{
	assert(cursor >= 0);

	int top = menu->top;
	int count = menu_count(menu);
	bool redraw = menu_should_redraw(menu);

	/* Keep a certain distance from the top when possible */
	if (cursor <= top) {
		top = cursor - 1;
	}

	/* Keep a certain distance from the bottom when possible */
	if (cursor >= top + (reg.h - 1)) {
		top = cursor - (reg.h - 1) + 1;
	}

	/* Limit the top to legal places */
	top = MAX(0, MIN(top, count - reg.h));

	if (top != menu->top) {
		menu->top = top;
		redraw = true;
	}

	/* Display "-more-" on the first/last row,
	 * if the menu has too many entries */
	int start_row = 0;
	if (top > 0 && menu_displays_more(menu)) {
		display_menu_more(menu, reg, start_row);
		start_row++;
	}
	int end_row = reg.h;
	if (top + reg.h < count && menu_displays_more(menu)) {
		end_row--;
		display_menu_more(menu, reg, end_row);
	}

	/* Position of cursor relative to top */
	int rel_cursor = cursor - menu->top;
	/* Previous position of cursor relative to top */
	int old_cursor = menu->old_cursor == MENU_CURSOR_INVALID ?
		MENU_CURSOR_INVALID : menu->old_cursor - menu->top;

	for (int row = start_row; row < end_row; row++) {
		if (row < count && (redraw
					|| row == rel_cursor
					|| row == old_cursor))
		{
			struct loc loc = {reg.x, reg.y + row};
			display_menu_row(menu, menu->top + row, row == rel_cursor, loc, reg.w);
		} else if (redraw) {
			Term_erase(reg.x, reg.y + row, reg.w);
		}
	}

	if (menu->cursor >= 0) {
		Term_cursor_to_xy(reg.x + menu->cursor_x_offset, reg.y + rel_cursor);
	}

	menu->old_cursor = cursor;
}

/*** Scrolling menu skin ***/

static ui_event scroll_skin_process_direction(struct menu *menu, int dir)
{
	ui_event out = EVENT_EMPTY;

	/* Reject diagonals */
	if (ddx[dir] && ddy[dir]) {
		return out;
	}

	if (ddx[dir]) {
		out.type = ddx[dir] < 0 ? EVT_ESCAPE : EVT_SELECT;
	} else if (ddy[dir]) {
		menu->cursor += ddy[dir];
		out.type = EVT_MOVE;
	}

	return out;
}

/**
 * Virtual function table for scrollable menu skin
 */
static const menu_skin menu_skin_scroll = {
	.get_cursor   = generic_skin_get_cursor,
	.display_list = generic_skin_display,
	.process_dir  = scroll_skin_process_direction
};

/*** Object menu skin ***/

static ui_event object_skin_process_direction(struct menu *menu, int dir)
{
	ui_event out = EVENT_EMPTY;

	/* Reject diagonals */
	if (ddx[dir] && ddy[dir]) {
		return out;
	}

	if (ddx[dir]) {
		out.type = EVT_SWITCH;
		out.key.code = ddx[dir] < 0 ? ARROW_LEFT : ARROW_RIGHT;
	} else if (ddy[dir]) {
		menu->cursor += ddy[dir];
		out.type = EVT_MOVE;
	}

	return out;
}

/**
 * Virtual function table for object menu skin
 */
static const menu_skin menu_skin_object = {
	.get_cursor   = generic_skin_get_cursor,
	.display_list = generic_skin_display,
	.process_dir  = object_skin_process_direction
};

/*** Multi-column menus ***/

#define MENU_DEFAULT_COLUMN_WIDTH 23

static int column_skin_get_cursor(struct menu *menu,
		struct loc loc, int count, int top, region reg)
{
	(void) top;

	if (!loc_in_region(loc, reg)) {
		return -1;
	}

	int cols = (count + reg.h - 1) / reg.h;
	int colw = menu->column_width > 0 ?
		menu->column_width : MENU_DEFAULT_COLUMN_WIDTH;

	if (colw * cols > reg.w) {
		colw = reg.w / cols;
	}

	assert(colw > 0);

	int cursor = (loc.y - reg.y) + reg.h * ((loc.x - reg.x) / colw);

	if (cursor >= count) {
		return -1;
	} else {
		return cursor;
	}
}

static void column_skin_display(struct menu *menu, int cursor, region reg)
{
	int count = menu_count(menu);
	int cols = (count + reg.h - 1) / reg.h;
	int colw = menu->column_width > 0 ?
		menu->column_width : MENU_DEFAULT_COLUMN_WIDTH;

	bool redraw = menu_should_redraw(menu);

	if (colw * cols > reg.w) {
		colw = reg.w / cols;
	}

	assert(colw > 0);

	for (int c = 0; c < cols; c++) {
		for (int r = 0; r < reg.h; r++) {
			int index = c * reg.h + r;

			if (index < count && (redraw
						|| index == cursor
						|| index == menu->old_cursor))
			{
				struct loc loc = {
					.x = reg.x + c * colw,
					.y = reg.y + r
				};

				display_menu_row(menu, index, index == cursor, loc, colw);
			}
		}
	}

	if (menu->cursor >= 0) {
		int x = reg.x + (cursor / reg.h) * colw;
		int y = reg.y + (cursor % reg.h);

		Term_cursor_to_xy(x + menu->cursor_x_offset, y);
	}

	menu->old_cursor = cursor;
}

static ui_event column_skin_process_direction(struct menu *menu, int dir)
{
	ui_event out = EVENT_EMPTY;

	/* Reject diagonals */
	if (ddx[dir] && ddy[dir]) {
		return out;
	}

	int count = menu_count(menu);
	int height = menu->active.h;
	int cols = (count + height - 1) / height;

	if (ddx[dir]) {
		menu->cursor += ddx[dir] * height;

		/* Adjust to the correct location */
		if (menu->cursor < 0) {
			menu->cursor = (height * cols) + menu->cursor;
			while (menu->cursor >= count) {
				menu->cursor -= height;
			}
		} else if (menu->cursor >= count) {
			menu->cursor = menu->cursor % height;
		}

		assert(menu->cursor >= 0);
		assert(menu->cursor < count);

		out.type = EVT_MOVE;

	} else if (ddy[dir]) {
		menu->cursor += ddy[dir];
		out.type = EVT_MOVE;
	}

	return out;
}

/**
 * Virtual function table for multi-column menu skin
 */
static const menu_skin menu_skin_columns = {
	.get_cursor   = column_skin_get_cursor,
	.display_list = column_skin_display,
	.process_dir  = column_skin_process_direction
};

/* ==================== GENERIC HELPER FUNCTIONS ==================== */

static bool is_valid_row(struct menu *menu, int index)
{
	if (index < 0 || index >= menu_count(menu)) {
		return false;
	} else if (menu->iter->valid_row) {
		return menu->iter->valid_row(menu, menu_index(menu, index));
	} else {
		return true;
	}
}

static bool has_valid_row(struct menu *menu)
{
	for (int i = 0, count = menu_count(menu); i < count; i++) {
		if (is_valid_row(menu, i)) {
			return true;
		}
	}

	return false;
}

static char code_from_key(const struct menu *menu,
		struct keypress key, bool caseless)
{
	char code = 0;

	if (key.code < CHAR_MAX) {
		code = menu_has_inscription(menu, key.code) ?
			menu->inscriptions[D2I(key.code)] : (char) key.code;
	}

	return caseless ? toupper(code) : code;
}

static bool tag_eq_code(int tag, int code, bool caseless)
{
	if (caseless) {
		tag = toupper(tag);
	}

	return tag != 0 && tag == code;
}

/**
 * Return a new position in the menu based on the key
 * pressed and the flags and various handler functions.
 */
static int get_cursor_key(struct menu *menu, struct keypress key)
{
	const bool caseless = menu_is_caseless(menu);
	const char code = code_from_key(menu, key, caseless);

	if (code != 0 && menu_has_tags(menu)) {
		if (menu->selections) {
			for (int i = 0, count = menu_count(menu); i < count; i++) {
				char tag = menu_get_tag(menu, i);
				if (tag_eq_code(tag, code, caseless)) {
					return i;
				} else if (tag == 0) {
					return -1;
				}
			}
		} else if (menu->iter->get_tag) {
			for (int i = 0, count = menu_count(menu); i < count; i++) {
				char tag = menu->iter->get_tag(menu, menu_index(menu, i));
				if (tag_eq_code(tag, code, caseless)) {
					return i;
				}
			}
		}
	}

	return -1;
}

/**
 * Modal display of menu
 */
static void display_menu_row(struct menu *menu,
		int i, bool cursor, struct loc loc, int width)
{
	int index = menu_index(menu, i);

	if (menu_displays_tags(menu)) {
		char tag = 0;

		if (menu->selections) {
			tag = menu_get_tag(menu, i);
		} else if (menu->iter->get_tag) {
			tag = menu->iter->get_tag(menu, index);
		}

		if (tag != 0) {
			char buf[4];
			strnfmt(buf, sizeof(buf), "%c) ", tag == 0 ? '?' : tag);

			Term_adds(loc.x, loc.y, 3, menu_row_style(true, cursor), buf);
		}

		width -= 3;
		loc.x += 3;
	}

	Term_erase(loc.x, loc.y, width);

	menu->iter->display_row(menu, index, cursor, loc, width);
}

static void display_menu_more(struct menu *menu, region reg, int row)
{
	const int y = reg.y + row;

	Term_erase(reg.x, y, reg.w);
	Term_addws(reg.x, y, reg.w, menu_row_style(false, false), L"-more-");
}

void menu_display(struct menu *menu)
{
	if (menu->browse_hook) {
		menu->browse_hook(menu_index(menu, menu->cursor),
				menu->menu_data, menu->active);
	}

	if (menu->title) {
		Term_adds(menu->boundary.x, menu->boundary.y, menu->boundary.w,
				COLOUR_WHITE, menu->title);
	}

	if (menu->header) {
		/* Above the menu */
		Term_adds(menu->active.x, menu->active.y - 1, menu->active.w,
				COLOUR_WHITE, menu->header);
	}

	if (menu->prompt) {
		/* Below the menu */
		put_str_h_simple(menu->prompt,
				loc(menu->boundary.x, menu->active.y + menu->active.h));
	}

	menu->skin->display_list(menu, menu->cursor, menu->active);

	Term_flush_output();
}

/*** MENU RUNNING AND INPUT HANDLING CODE ***/

/**
 * Handle mouse input in a menu.
 * 
 * Mouse output is either moving, selecting, escaping, or nothing.
 * Returns true if something changes as a result of the click.
 */
void menu_handle_mouse(struct menu *menu,
		struct mouseclick mouse, ui_event *out)
{
	if (mouse.button == MOUSE_BUTTON_RIGHT) {
		out->type = EVT_ESCAPE;
	} else if (mouse_in_region(mouse, menu->active)) {
		int new_cursor = menu->skin->get_cursor(menu, loc(mouse.x, mouse.y),
				menu_count(menu), menu->top, menu->active);
	
		if (is_valid_row(menu, new_cursor)) {
			if (!mnflag_has(menu->flags, MN_DBL_TAP)
					|| new_cursor == menu->cursor)
			{
				out->type = EVT_SELECT;
			} else {
				out->type = EVT_MOVE;
			}
			menu->cursor = new_cursor;
		}
	}
}

/**
 * Handle any menu command keys
 */
static bool menu_handle_action(struct menu *menu, const ui_event *in)
{
	if (menu->iter->row_handler) {
		return menu->iter->row_handler(menu,
				in, menu_index(menu, menu->cursor));
	} else {
		return false;
	}
}
/**
 * Handle navigation keypresses.
 *
 * Returns true if they key was intelligible as navigation,
 * regardless of whether any action was taken.
 */
void menu_handle_keypress(struct menu *menu,
		struct keypress key, ui_event *out)
{
	const int count = menu_count(menu);

	/* Get the new cursor position from the menu item tags */
	int new_cursor = get_cursor_key(menu, key);
	if (is_valid_row(menu, new_cursor)) {
		if (!menu_needs_double_tap(menu)
				|| new_cursor == menu->cursor)
		{
			out->type = EVT_SELECT;
		} else {
			out->type = EVT_MOVE;
		}
		menu->cursor = new_cursor;

	} else if (key.code == ESCAPE) {
		/* Escape stops us here */
		out->type = EVT_ESCAPE;
	} else if (key.code == ' ') {
		if (menu->active.h < count) {
			/* Go to start of next page */
			menu->cursor += menu->active.h;
			menu->top = menu->cursor;

			if (menu->cursor >= count - 1) {
				menu->cursor = 0;
			}

			out->type = EVT_MOVE;
		}
	} else if (key.code == KC_ENTER) {
		out->type = EVT_SELECT;
	} else {
		/* Try directional movement */
		int dir = target_dir(key);

		if (dir != 0 && has_valid_row(menu)) {
			*out = menu->skin->process_dir(menu, dir);

			if (out->type == EVT_MOVE && ddy[dir] != 0) {
				while (!is_valid_row(menu, menu->cursor)) {
					/* Loop around */
					if (menu->cursor > count - 1) {
						menu->cursor = 0;
					} else if (menu->cursor < 0) {
						menu->cursor = count - 1;
					} else {
						menu->cursor += ddy[dir];
					}
				}
			}
			
			assert(menu->cursor >= 0);
			assert(menu->cursor < count);
		}
	}
}

/**
 * Check if this event should terminate a menu.
 *
 * Returns true if the menu should stop running.
 */
static bool menu_stop_event(ui_event event)
{
	switch (event.type) {
		case EVT_SELECT: case EVT_ESCAPE: case EVT_SWITCH:
			return true;
		default:
			return false;
	}
}

/**
 * Run a menu.
 */
ui_event menu_select(struct menu *menu)
{
	assert(menu->active.w != 0);
	assert(menu->active.h != 0);

	menu_force_redraw(menu);

	const bool action_ok = menu_can_act(menu);

	ui_event in = EVENT_EMPTY;

	while (!menu_stop_event(in)) {
		ui_event out = EVENT_EMPTY;

		menu_display(menu);
		in = inkey_simple();

		/* Handle mouse and keyboard commands */
		if (in.type == EVT_MOUSE) {
			if (action_ok && menu_handle_action(menu, &in)) {
				continue;
			}

			menu_handle_mouse(menu, in.mouse, &out);
		} else if (in.type == EVT_KBRD) {
			if (action_ok) {
				if (menu->command_keys
						&& in.key.code < CHAR_MAX
						&& strchr(menu->command_keys, (int) in.key.code))
				{
					/* Command key */
					if (menu_handle_action(menu, &in)) {
						continue;
					}
				}
				if (menu->stop_keys
						&& in.key.code < CHAR_MAX
						&& strchr(menu->stop_keys, (int) in.key.code))
				{
					/* Stop key */
					if (menu_handle_action(menu, &in)) {
						continue;
					} else {
						break;
					}
				}
			}

			menu_handle_keypress(menu, in.key, &out);
		}

		/* If we've selected something, then try to handle that */
		if (out.type == EVT_SELECT
				&& action_ok
				&& menu_handle_action(menu, &out))
		{
			continue;
		}

		/* Notify about the outgoing type */
		in = out;
	}

	return in;
}

/* ==================== MENU ACCESSORS ==================== */

/**
 * Return the menu iter struct for a given iter ID.
 */
const menu_iter *menu_find_iter(menu_iter_id id)
{
	switch (id) {
		case MN_ITER_ACTIONS:
			return &menu_iter_actions;

		case MN_ITER_STRINGS:
			return &menu_iter_strings;

		default:
			return NULL;
	}
}

/**
 * Return the skin behaviour struct for a given skin ID.
 */
static const menu_skin *menu_find_skin(skin_id id)
{
	switch (id) {
		case MN_SKIN_SCROLL:
			return &menu_skin_scroll;

		case MN_SKIN_OBJECT:
			return &menu_skin_object;

		case MN_SKIN_COLUMNS:
			return &menu_skin_columns;

		default:
			return NULL;
	}
}

static void menu_ensure_cursor_valid(struct menu *menu)
{
	int count = menu_count(menu);

	for (int row = menu->cursor; row < count; row++) {
		if (is_valid_row(menu, row)) {
			menu->cursor = row;
			return;
		}
	}

	/* If we've run off the end, without finding a valid row, put cursor
	 * on the last row */
	menu->cursor = MAX(0, count - 1);
}

void menu_set_filter(struct menu *menu, const int *filter_list, int count)
{
	menu->filter_list = filter_list;
	menu->filter_count = count;

	menu_ensure_cursor_valid(menu);
}

void menu_release_filter(struct menu *menu)
{
	menu->filter_list = NULL;
	menu->filter_count = 0;

	menu_ensure_cursor_valid(menu);
}

/* ==================== MENU INITIALIZATION ==================== */

void menu_layout(struct menu *menu, region reg)
{
	menu->boundary = region_calculate(reg);
	menu->active = menu->boundary;

	if (menu->title) {
		/* Shrink the menu, move it down and to the right */
		menu->active.y += 2;
		menu->active.h -= 2;
		menu->active.x += 1;
		menu->active.w -= 1;
	}

	if (menu->header) {
		/* Header is right above the menu */
		menu->active.y++;
		menu->active.h--;
	}

	if (menu->prompt) {
		/* Prompt is normally right below the menu */
		if (menu->active.h > 1) {
			menu->active.h--;
		} else {
			int offset = strlen(menu->prompt) + 1;
			menu->active.x += offset;
			menu->active.w -= offset;
		}
	}

	assert(menu->active.w > 0);
	assert(menu->active.h > 0);
}

void menu_layout_term(struct menu *menu)
{
	region full = {0};

	/* Make menu as big as the whole term */
	menu_layout(menu, full);
}

void menu_setpriv(struct menu *menu, int count, void *data)
{
	menu->count = count;
	menu->menu_data = data;

	menu_ensure_cursor_valid(menu);
}

void *menu_priv(struct menu *menu)
{
	return menu->menu_data;
}

void menu_init(struct menu *menu, skin_id skin_id, const menu_iter *iter)
{
	const menu_skin *skin = menu_find_skin(skin_id);
	assert(skin != NULL);
	assert(iter != NULL);

	memset(menu, 0, sizeof(*menu));

	/* Pedantry */
	menu->header       = NULL;
	menu->title        = NULL;
	menu->prompt       = NULL;
	menu->selections   = NULL;
	menu->inscriptions = NULL;
	menu->command_keys = NULL;
	menu->stop_keys    = NULL;
	menu->browse_hook  = NULL;
	menu->filter_list  = NULL;

	menu->skin = skin;
	menu->iter = iter;
}

struct menu *menu_new(skin_id skin_id, const menu_iter *iter)
{
	struct menu *menu = mem_alloc(sizeof(*menu));
	menu_init(menu, skin_id, iter);

	return menu;
}

struct menu *menu_new_action(menu_action *acts, size_t count)
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, menu_find_iter(MN_ITER_ACTIONS));
	menu_setpriv(menu, count, acts);

	return menu;
}

void menu_free(struct menu *menu)
{
	mem_free(menu);
}

void menu_set_cursor_x_offset(struct menu *menu, int offset)
{
	menu->cursor_x_offset = offset;
}

/* ==================== DYNAMIC MENU HANDLING ==================== */

struct menu_entry {
	char *text;
	int value;
	bool valid;

	struct menu_entry *next;
};

static bool dynamic_valid(struct menu *menu, int index)
{
	struct menu_entry *entry = menu_priv(menu);

	while (index > 0) {
		assert(entry->next);
		entry = entry->next;
		index--;
	}

	return entry->valid;
}

static void dynamic_display(struct menu *menu,
		int index, bool cursor, struct loc loc, int width)
{
	struct menu_entry *entry = menu_priv(menu);

	while (index > 0) {
		assert(entry->next);
		entry = entry->next;
		index--;
	}

	Term_adds(loc.x, loc.y, width,
			menu_row_style(true, cursor), entry->text);
}

static const menu_iter dynamic_iter = {
	.valid_row = dynamic_valid,
	.display_row = dynamic_display
};

struct menu *menu_dynamic_new(void)
{
	struct menu *menu = menu_new(MN_SKIN_SCROLL, &dynamic_iter);
	menu_setpriv(menu, 0, NULL);

	return menu;
}

void menu_dynamic_add_valid(struct menu *menu,
		const char *text, int value, bool valid)
{
	assert(menu->iter == &dynamic_iter);

	struct menu_entry *head = menu_priv(menu);
	struct menu_entry *new = mem_zalloc(sizeof(*new));

	new->text = string_make(text);
	new->value = value;
	new->valid = valid;

	if (head) {
		struct menu_entry *tail = head;
		while (tail->next) {
			tail = tail->next;
		}

		tail->next = new;
		menu_setpriv(menu, menu->count + 1, head);
	} else {
		menu_setpriv(menu, menu->count + 1, new);
	}
}

void menu_dynamic_add(struct menu *menu, const char *text, int value)
{
	menu_dynamic_add_valid(menu, text, value, true);
}

void menu_dynamic_add_label_valid(struct menu *menu,
		const char *text, const char label, int value, char *label_list, bool valid)
{
	if (label) {
		assert(menu->selections != NULL);
		assert(menu->selections == label_list);

		label_list[menu->count] = label;
	}

	menu_dynamic_add_valid(menu, text, value, valid);
}

void menu_dynamic_add_label(struct menu *menu,
		const char *text, const char label, int value, char *label_list)
{
	menu_dynamic_add_label_valid(menu, text, label, value, label_list, true);
}

size_t menu_dynamic_longest_entry(struct menu *menu)
{
	size_t maxlen = 0;

	for (struct menu_entry *entry = menu_priv(menu);
			entry != NULL;
			entry = entry->next)
	{
		size_t len = strlen(entry->text);

		if (len > maxlen) {
			maxlen = len;
		}
	}

	return maxlen;
}

region menu_dynamic_calc_location(struct menu *menu)
{
	int tag_space = menu_displays_tags(menu) ? 3 : 0;

	region reg = {
		.x = 0,
		.y = 0,
		.w = menu_dynamic_longest_entry(menu) + tag_space,
		.h = menu->count
	};

	return reg;
}

int menu_dynamic_select(struct menu *menu)
{
	ui_event e = menu_select(menu);

	if (e.type == EVT_ESCAPE) {
		return -1;
	} else {
		struct menu_entry *entry = menu_priv(menu);
		int cursor = menu->cursor;

		while (cursor > 0) {
			assert(entry->next);
			entry = entry->next;
			cursor--;
		}	

		return entry->value;
	}
}

void menu_dynamic_free(struct menu *menu)
{
	struct menu_entry *entry = menu_priv(menu);

	while (entry) {
		struct menu_entry *next = entry->next;
		string_free(entry->text);
		mem_free(entry);
		entry = next;
	}

	mem_free(menu);
}
