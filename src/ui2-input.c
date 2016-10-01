/**
 * \file ui2-input.c
 * \brief Some high-level UI functions, inkey()
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

#include "angband.h"
#include "cmds.h"
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-path.h"
#include "randname.h"
#include "savefile.h"
#include "target.h"
#include "ui2-command.h"
#include "ui2-context.h"
#include "ui2-curse.h"
#include "ui2-display.h"
#include "ui2-help.h"
#include "ui2-keymap.h"
#include "ui2-knowledge.h"
#include "ui2-map.h"
#include "ui2-object.h"
#include "ui2-output.h"
#include "ui2-player.h"
#include "ui2-prefs.h"
#include "ui2-signals.h"
#include "ui2-spell.h"
#include "ui2-store.h"
#include "ui2-target.h"

static struct {
	/**
	 * Remember the flush, and in the next call to inkey_full(),
	 * perform the actual flushing, for efficiency, and
	 * correctness of the inkey_full() function.
	 */
	bool flush;
	/**
	 * This special array allows a sequence of keys to be inserted into
	 * the stream of keys returned by inkey_full().
	 * We use it to implement keymaps.
	 */
	struct {
		struct keypress keys[KEYMAP_ACTION_MAX];
		/* currently pending keypress */
		size_t key;
		/* number of remaining keypresses */
		size_t number;
	} keymap;
	/**
	 * See if "-more-" propmts will be skipped while in a keymap.
	 */
	bool auto_more;
} inkey_state;

static struct keypress inkey_state_take_key(void)
{
	assert(inkey_state.keymap.number > 0);

	struct keypress key =
		inkey_state.keymap.keys[inkey_state.keymap.key];

	inkey_state.keymap.key++;
	inkey_state.keymap.number--;

	return key;
}

static void inkey_state_add_keymap(const struct keypress *keys, size_t number)
{
	assert(inkey_state.keymap.number == 0);
	assert(number <= N_ELEMENTS(inkey_state.keymap.keys));

	for (size_t i = 0; i < number; i++) {
		inkey_state.keymap.keys[i] = keys[i];
	}

	inkey_state.keymap.key = 0;
	inkey_state.keymap.number = number;
}

static bool inkey_state_has_keymap(void)
{
	return inkey_state.keymap.number > 0;
}

static void inkey_state_flush_keymap(void)
{
	inkey_state.keymap.key = 0;
	inkey_state.keymap.number = 0;
	memset(inkey_state.keymap.keys, 0, sizeof(inkey_state.keymap.keys));
}

static void inkey_state_auto_more_on(void)
{
	inkey_state.auto_more = true;
}

static void inkey_state_auto_more_off(void)
{
	inkey_state.auto_more = false;
}

bool auto_more(void)
{
	return OPT(auto_more) || inkey_state.auto_more;
}

static void inkey_state_check_flush(void)
{
	if (inkey_state.flush) {
		Term_flush_events();
		inkey_state_flush_keymap();
		inkey_state.flush = false;
	}
}

void inkey_flush(game_event_type type, game_event_data *data, void *user)
{
	(void) type;
	(void) data;
	(void) user;

	inkey_state.flush = true;
}

/**
 * Helper function called only from "inkey()"
 */
static bool inkey_keymap(ui_event *event)
{
	if (!inkey_state_has_keymap()) {
		inkey_state_auto_more_off();
		return false;
	}

	struct keypress key = inkey_state_take_key();

	/* Peek at the key, and see if we want to skip more prompts */
	if (key.code == '(') {
		inkey_state_auto_more_on();
		return inkey_keymap(event);
	} else if (key.code == ')') {
		inkey_state_auto_more_off();
		return inkey_keymap(event);
	} else {
		event->key = key;
		return true;
	}
}

/**
 * Get a keypress from the user.
 */
ui_event inkey_full(bool instant, bool wait, int scans)
{
	inkey_state_check_flush();

	ui_event event = EVENT_EMPTY;

	if (inkey_keymap(&event)) {
		return event;
	}

	if (instant) {
		Term_take_event(&event);
		return event;
	}
	
	if (!Term_check_event(NULL)) {
		Term_redraw_screen();
	}

	if (wait) {
		Term_wait_event(&event);
	} else {
		assert(scans > 0);
		for (int s = 0; s < scans && !Term_take_event(&event); s++) {
			Term_delay(INKEY_SCAN_PERIOD);
		}
	}

	return event;
}

/**
 * Get a "keypress" from the user.
 */
struct keypress inkey_only_key(void)
{
	ui_event event = EVENT_EMPTY;

	while (event.type != EVT_ESCAPE
			&& event.type != EVT_KBRD
			&& event.type != EVT_MOUSE)
	{
		event = inkey_simple();
	}

	/* Make the event a keypress */
	if (event.type == EVT_ESCAPE) {
		event.type = EVT_KBRD;
		event.key.code = ESCAPE;
		event.key.mods = 0;
	} else if (event.type == EVT_MOUSE) {
		if (event.mouse.button == MOUSE_BUTTON_LEFT) {
			event.type = EVT_KBRD;
			event.key.code = '\n';
			event.key.mods = 0;
		} else {
			event.type = EVT_KBRD;
			event.key.code = ESCAPE;
			event.key.mods = 0;
		}
	}

	return event.key;
}

/**
 * Get a keypress or a mousepress from the user.
 */
ui_event inkey_mouse_or_key(void)
{
	ui_event event = EVENT_EMPTY;

	while (event.type != EVT_ESCAPE
			&& event.type != EVT_KBRD
			&& event.type != EVT_MOUSE)
	{
		event = inkey_simple();
	}

	if (event.type == EVT_ESCAPE) {
		event.type = EVT_KBRD;
		event.key.code = ESCAPE;
		event.key.mods = 0;
	}

	return event;
}

ui_event inkey_wait(int scans)
{
	if (scans > 0) {
		return inkey_full(false, false, scans);
	} else {
		return inkey_full(true, false, 0);
	}
}

ui_event inkey_simple(void)
{
	return inkey_full(false, true, 0);
}

/**
 * Get a keypress or mouse click from the user and ignore it.
 */
void inkey_any(void)
{
	ui_event event = EVENT_EMPTY;

	/* Only accept a keypress or mouse click */
	while (event.type != EVT_MOUSE && event.type != EVT_KBRD) {
		event = inkey_simple();
	}
}

/**
 * The default keypress handling function for askfor_aux, this takes the
 * given keypress, input buffer, length, etc, and does the appropriate action
 * for each keypress, such as moving the cursor left or inserting a character.
 *
 * It should return true when editing of the buffer is "complete" (e.g. on
 * the press of RETURN).
 */
bool askfor_aux_keypress(char *buf, size_t buflen,
		size_t *curs, size_t *len,
		struct keypress keypress, bool firsttime)
{
	assert(*curs <= *len);
	assert(*len < buflen);

	switch (keypress.code) {
		case ESCAPE:
			*curs = 0;
			return true;
		
		case KC_ENTER:
			*curs = *len;
			return true;
		
		case ARROW_LEFT:
			if (firsttime) {
				*curs = 0;
			} else if (*curs > 0) {
				(*curs)--;
			}
			break;
		
		case ARROW_RIGHT:
			if (firsttime) {
				*curs = *len;
			} else if (*curs < *len) {
				(*curs)++;
			}
			break;
		
		case KC_BACKSPACE: case KC_DELETE:
			/* If this is the first time round, backspace means "delete all" */
			if (firsttime) {
				buf[0] = 0;
				*curs = 0;
				*len = 0;
				break;
			}

			/* Refuse to backspace into oblivion */
			if ((keypress.code == KC_BACKSPACE && *curs == 0)
					|| (keypress.code == KC_DELETE && *curs == *len))
			{
				break;
			}

			if (keypress.code == KC_BACKSPACE) {
				memmove(buf + *curs - 1, buf + *curs, *len - *curs);
				(*curs)--;
			} else {
				memmove(buf + *curs, buf + *curs + 1, *len - *curs - 1);
			}

			(*len)--;
			buf[*len] = 0;
			break;
		
		default:
			if (!isprint(keypress.code)) {
				bell("Illegal edit key!");
				break;
			}

			/* Clear the buffer if this is the first time round */
			if (firsttime) {
				buf[0] = 0;
				*curs = 0;
				*len = 0;
			}

			if (*len + 1 < buflen) {
				if (*curs < *len) {
					/* Move the rest of the buffer along to make room */
					memmove(buf + *curs + 1, buf + *curs, *len - *curs);
				}

				/* Insert the character */
				buf[*curs] = (char) keypress.code;
				(*curs)++;
				(*len)++;
				buf[*len] = 0;
			}
			break;
	}

	/* By default, we aren't done. */
	return false;
}

/**
 * Helper function called from askfor_aux()
 */
static bool askfor_aux_handle(struct loc loc, struct keypress *key,
		char *buf, size_t buflen, size_t *curs, size_t *len,
		bool firsttime, askfor_aux_handler handler)
{
	Term_erase(loc.x, loc.y, buflen);

	if (*len > 0) {
		Term_adds(loc.x, loc.y, *len,
				firsttime ? COLOUR_YELLOW : COLOUR_WHITE, buf);
	}

	Term_cursor_to_xy(loc.x + *curs, loc.y);
	Term_flush_output();

	*key = inkey_only_key();

	return handler(buf, buflen, curs, len, *key, firsttime);
}

/**
 * Get some input at the cursor location.
 *
 * The buffer is assumed to have been initialized to a default string.
 * Note that this string is often empty (see below).
 *
 * The default buffer is displayed in yellow until cleared, which happens
 * on the first keypress, unless that keypress is Return.
 *
 * Normal chars clear the default and append the char.
 * Backspace clears the default or deletes the final char.
 * Return accepts the current buffer contents and returns true.
 * Escape clears the buffer and the window and returns false.
 *
 * Note that 'buflen' refers to the size of the buffer. The maximum length
 * of the input is 'buflen' - 1.
 *
 * 'handler' is a pointer to a function to handle keypresses, altering
 * the input buffer, cursor position and suchlike as required.
 * See askfor_aux_keypress() (the default handler if you supply NULL for
 * 'handler') for an example.
 */
bool askfor_aux(char *buf, size_t buflen, askfor_aux_handler handler)
{
	display_term_push(DISPLAY_MESSAGE_LINE);
	Term_cursor_visible(true);

	size_t len = strlen(buf);
	size_t curs = 0;

	struct keypress key;

	struct loc loc;
	Term_get_cursor(&loc.x, &loc.y, NULL, NULL);

	bool done = askfor_aux_handle(loc, &key, buf, buflen, &curs, &len, true,
			handler ? handler : askfor_aux_keypress);

	while (!done) {
		done = askfor_aux_handle(loc, &key, buf, buflen, &curs, &len, false,
				handler ? handler : askfor_aux_keypress);
	}

	Term_cursor_visible(false);
	display_term_pop();

	/* ESCAPE means "cancel" and we return false then */
	return key.code == ESCAPE ? false : true;
}

/**
 * A keypress handling function for askfor_aux, that handles the special
 * case of '*' for a new random name and passes any other keypress
 * through to the default editing handler.
 */
static bool get_name_keypress(char *buf, size_t buflen,
		size_t *curs, size_t *len,
		struct keypress keypress, bool firsttime)
{
	switch (keypress.code) {
		case '*':
			*len = randname_make(RANDNAME_TOLKIEN, 4, 8,
					buf, buflen, name_sections);
			my_strcap(buf);
			*curs = 0;
			return false;

		default:
			return askfor_aux_keypress(buf, buflen,
					curs, len, keypress, firsttime);
	}
}

/**
 * Gets a name for the character, reacting to name changes.
 * If sf is true, we change the savefile name depending on the character name.
 */
bool get_character_name(char *buf, size_t buflen)
{
	my_strcpy(buf, op_ptr->full_name, buflen);

	show_prompt("Enter a name for your character (* for a random name): ", false);
	bool res = askfor_aux(buf, buflen, get_name_keypress);
	clear_prompt();

	return res;
}

/**
 * Prompt for a string from the user.
 * The prompt should take the form "Prompt: ".
 * See askfor_aux() for some notes about buf and buflen,
 * and about the return value of this function.
 */
bool textui_get_string(const char *prompt, char *buf, size_t buflen)
{
	event_signal(EVENT_MESSAGE_FLUSH);

	show_prompt(prompt, false);
	bool res = askfor_aux(buf, buflen, NULL);
	clear_prompt();

	return res;
}

/**
 * Request a quantity from the user.
 */
int textui_get_quantity(const char *prompt, int max)
{
	int amt = 1;

	/* Prompt if needed */
	if (max > 1) {
		char buf[ANGBAND_TERM_STANDARD_WIDTH] = "1";
		char tmp[ANGBAND_TERM_STANDARD_WIDTH];

		/* Build a prompt if needed */
		if (prompt == NULL) {
			strnfmt(tmp, sizeof(tmp), "Quantity (0-%d, *=all): ", max);
			prompt = tmp;
		}

		/* Up to six digits (999999 maximum) */
		if (get_string(prompt, buf, sizeof("123456"))) {
			if (buf[0] == '*' || isalpha((unsigned char) buf[0])) {
				amt = max; /* A star or letter means "all" */
			} else {
				amt = atoi(buf);
			}
		} else {
			amt = 0;
		}
	}

	return MAX(0, MIN(amt, max));
}

/**
 * Verify something with the user.
 * The prompt should take the form "Query? "
 * Note that "[y/n]" is appended to the prompt.
 */
bool textui_get_check(const char *prompt)
{
	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf), "%.60s[y/n]", prompt);

	show_prompt(buf, false);
	ui_event event = inkey_mouse_or_key();
	clear_prompt();

	if (event.type == EVT_MOUSE
			&& event.mouse.button == MOUSE_BUTTON_LEFT)
	{
		return true;
	} else if (event.type == EVT_KBRD
			&& (event.key.code == 'Y' || event.key.code == 'y'))
	{
		return true;
	}

	return false;
}

/**
 * Ask the user to respond with a character. Options is a constant string,
 * e.g. "yns"; len is the length of the constant string, and fallback should
 * be the default answer if the user hits escape or an invalid key.
 *
 * Example: get_char("Study? ", "yns", 3, 'n')
 *     This prompts "Study? [yns]" and defaults to 'n'.
 */
char get_char(const char *prompt, const char *options, size_t len, char fallback)
{
	(void) len;

	char buf[ANGBAND_TERM_STANDARD_WIDTH];
	strnfmt(buf, sizeof(buf), "%.60s[%s] ", prompt, options);

	show_prompt(prompt, false);
	struct keypress key = inkey_only_key();
	clear_prompt();

	if (key.code < CHAR_MAX) {
		key.code = tolower(key.code);
		/* See if key is in our options string */
		if (!strchr(options, key.code)) {
			key.code = fallback;
		}
	} else {
		key.code = fallback;
	}

	return key.code;
}

/**
 * Text-native way of getting a filename.
 */
static bool get_file_text(const char *suggested_name, char *path, size_t len)
{
	char buf[1024];

	/* Get filename */
	my_strcpy(buf, suggested_name, sizeof(buf));
	if (!get_string("File name: ", buf, sizeof(buf))) {
		return false;
	}

	/* Make sure it's actually a filename */
	if (buf[0] == 0) {
		return false;
	}

	path_build(path, len, ANGBAND_DIR_USER, buf);

	if (file_exists(path) && !get_check("Replace existing file? ")) {
		return false;
	}

	show_prompt(format("Saving as %s.", path), false);

	inkey_any();

	clear_prompt();

	return true;
}

/**
 * Get a pathname to save a file to, given the suggested name.  Returns the
 * result in path.
 */
bool (*get_file)(const char *suggested_name, char *path, size_t len) = get_file_text;

static bool get_mouse_or_key(const char *prompt, ui_event *command)
{
	show_prompt(prompt, false);

	*command = inkey_mouse_or_key();

	clear_prompt();

	if (command->type == EVT_KBRD && command->key.code == ESCAPE) {
		return false;
	} else {
		return true;
	}
}

/**
 * Prompts for a keypress
 * The prompt should take the form "Command: "
 * Returns true unless the character is "Escape",
 * or otherwise not represantable as char
 */
bool textui_get_com(const char *prompt, char *command)
{
	show_prompt(prompt, true);

	struct keypress key = inkey_only_key();

	clear_prompt();

	if (key.code < CHAR_MAX && key.code != ESCAPE) {
		*command = (char) key.code;
		return true;
	} else {
		*command = 0;
		return false;
	}
}

static int dir_transitions[10][10] = {
	/* 0-> */ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
	/* 1-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 2-> */ { 0, 0, 2, 0, 1, 0, 3, 0, 5, 0 },
	/* 3-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 4-> */ { 0, 0, 1, 0, 4, 0, 5, 0, 7, 0 },
	/* 5-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 6-> */ { 0, 0, 3, 0, 5, 0, 6, 0, 9, 0 },
	/* 7-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	/* 8-> */ { 0, 0, 5, 0, 7, 0, 9, 0, 8, 0 },
	/* 9-> */ { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

/**
 * Request a movement direction (1,2,3,4,6,7,8,9) from the user.
 *
 * Return true if a direction was chosen, otherwise return false.
 *
 * This function should be used for all repeatable commands, such as
 * run, walk, open, close, bash, disarm, spike, tunnel, etc, as well
 * as all commands which must reference a grid adjacent to the player,
 * and which may not reference the grid under the player.
 *
 * Directions "5" and "0" are illegal and will not be accepted.
 */
bool textui_get_rep_dir(int *dp, bool allow_5)
{
	*dp = 0;

	int dir = 0;

	while (dir == 0) {
		/* Get first keypress - the first test is to avoid displaying the
		 * prompt for direction if there's already a keypress queued up
		 * and waiting - this just avoids a flickering prompt if there is
		 * a "lazy" movement delay. */
		ui_event event = inkey_wait(0);

		if (event.type == EVT_NONE
				|| (event.type == EVT_KBRD && target_dir(event.key) == 0))
		{
			show_prompt("Direction or <click> (Escape to cancel)?", false);
			event = inkey_simple();
		}

		/* Check mouse coordinates, or get keypresses until a dir is chosen */
		if (event.type == EVT_MOUSE) {
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				struct loc from = loc(player->px, player->py);
				struct loc to = loc(EVENT_GRID_X(event), EVENT_GRID_Y(event));

				dir = pathfind_direction_to(from, to);
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				clear_prompt();
				return false;
			}
		} else if (event.type == EVT_KBRD) {
			int keypresses_handled = 0;

			while (event.type == EVT_KBRD && event.key.code != 0) {
				if (event.key.code == ESCAPE) {
					clear_prompt();
					return false;
				}

				dir = dir_transitions[dir][target_dir_allow(event.key, allow_5)];

				if (dir == 0
						|| op_ptr->lazymove_delay == 0
						|| ++keypresses_handled > 1)
				{
					break;
				}

				event = inkey_wait(op_ptr->lazymove_delay);
			}

			/* 5 is equivalent to "escape" */
			if (dir == 5 && !allow_5) {
				clear_prompt();
				return false;
			}
		}

		if (dir == 0) {
			bell("Illegal repeatable direction!");
		}
	}

	clear_prompt();
	*dp = dir;

	return true;
}

/**
 * Get an aiming direction (1,2,3,4,6,7,8,9 or 5) from the user.
 * Return true if a direction was chosen, otherwise return false.
 * The direction "5" is special, and means "use current target".
 *
 * Note that "use old target", if set, will pre-empt user interaction,
 * if there is a usable target already set.
 */
bool textui_get_aim_dir(int *dp)
{
	*dp = 0;

	int dir = OPT(use_old_target) && target_okay() ? 5 : 0;

	while (dir == 0) {
		const char *prompt = target_okay() ?
			"Direction ('5' for target, '*' or <click> to re-target, Escape to cancel)? " :
			"Direction ('*' or <click> to target, \"'\" for closest, Escape to cancel)? ";

		ui_event event;
		if (!get_mouse_or_key(prompt, &event)) {
			break;
		}

		if (event.type == EVT_MOUSE) {
			if (event.mouse.button == MOUSE_BUTTON_LEFT) {
				dir = target_set_interactive(TARGET_KILL,
							loc(EVENT_GRID_X(event), EVENT_GRID_Y(event))) ? 5 : 0;
			} else if (event.mouse.button == MOUSE_BUTTON_RIGHT) {
				break;
			}
		} else if (event.type == EVT_KBRD) {
			int keypresses_handled = 0;

			switch (event.key.code) {
				case '*':
					dir = target_set_interactive(TARGET_KILL, loc(-1, -1)) ? 5 : 0;
					break;

				case '\'':
					dir = target_set_closest(TARGET_KILL) ? 5 : 0;
					break;

				case 't': case '5': case '0': case '.':
					dir = target_okay() ? 5 : 0;
					break;

				default:
					while (event.type == EVT_KBRD && event.key.code != 0) {
						dir = dir_transitions[dir][target_dir(event.key)];

						if (dir == 0
								|| op_ptr->lazymove_delay == 0
								|| ++keypresses_handled > 1)
						{
							break;
						}

						event = inkey_wait(op_ptr->lazymove_delay);
					}
					break;
			}
		}

		if (dir == 0) {
			bell("Illegal aim direction!");
		}
	}

	clear_prompt();
	*dp = dir;

	return dir != 0;
}

/**
 * Initialise the UI hooks to give input asked for by the game
 */
void textui_input_init(void)
{
	get_string_hook = textui_get_string;
	get_quantity_hook = textui_get_quantity;
	get_check_hook = textui_get_check;
	get_com_hook = textui_get_com;
	get_rep_dir_hook = textui_get_rep_dir;
	get_aim_dir_hook = textui_get_aim_dir;
	get_spell_from_book_hook = textui_get_spell_from_book;
	get_spell_hook = textui_get_spell;
	get_item_hook = textui_get_item;
	get_curse_hook = textui_get_curse;
	get_panel_hook = textui_get_panel;
	panel_contains_hook = textui_panel_contains;
	map_is_visible_hook = textui_map_is_visible;
}

/**
 * Get a command count, with the '0' key.
 */
static int textui_get_count(void)
{
	int count = 0;

	while (true) {
		show_prompt(format("Repeat: %d", count), true);

		struct keypress key = inkey_only_key();
		if (key.code == ESCAPE) {
			return -1;
		} else if (key.code == KC_DELETE || key.code == KC_BACKSPACE) {
			count = count / 10;
		} else if (isdigit((unsigned char) key.code)) {
			int new_count = count * 10 + D2I(key.code);

			if (new_count > 9999) {
				bell("Invalid repeat count!");
			} else {
				count = new_count;
			}
		} else {
			if (key.code != KC_ENTER) {
				Term_keypress(key.code, key.mods);
			}
			break;
		}
	}

	return count;
}

static void textui_get_command_aux(ui_event *event,
		int *count, const struct keypress **keymap)
{
	assert(event->type == EVT_KBRD);

	char ch = 0;
	int cnt = 0;

	switch (event->key.code) {
		case '0':
			/* Allow repeat count to be entered */
			cnt = textui_get_count();
			if (cnt > 0 && get_mouse_or_key("Command: ", event)) {
				*count = cnt;
			} else {
				event->type = EVT_NONE;
				return;
			}
			break;

		case '^':
			/* Allow control chars to be entered */
			if (get_com("Control: ", &ch)) {
				event->key.code = KTRL(ch);
			} else {
				event->type = EVT_NONE;
				return;
			}
			break;

		case '\\':
			/* Allow keymaps to be bypassed */
			if (!get_mouse_or_key("Command: ", event)) {
				event->type = EVT_NONE;
			}
			return;
	}

	if (keymap != NULL) {
		*keymap = keymap_find(KEYMAP_MODE_OPT, event->key);
	}
}

/**
 * Request a command from the user.
 *
 * Note that "caret" ("^") is treated specially, and is used to
 * allow manual input of control characters.  This can be used
 * on many machines to request repeated tunneling (Ctrl-H) and
 * on the Macintosh to request "Control-Caret".
 *
 * Note that "backslash" is treated specially, and is used to bypass any
 * keymap entry for the following character.  This is useful for macros.
 */
ui_event textui_get_command(int *count)
{
	ui_event event = EVENT_EMPTY;

	while (event.type == EVT_NONE) {
		const struct keypress *keymap = NULL;

		event = inkey_simple();

		if (event.type == EVT_KBRD) {
			/* Don't apply keymap if inside a keymap already */
			textui_get_command_aux(&event, count,
					inkey_state_has_keymap() ? NULL : &keymap);
		}

		clear_prompt();

		if (keymap != NULL) {
			size_t n = 0;
			while (keymap[n].type) {
				n++;
			}
			inkey_state_add_keymap(keymap, n);
			event.type = EVT_NONE;
		}
	}

	return event;
}

/**
 * Check no currently worn items are stopping the action 'c'
 */
bool key_confirm_command(unsigned char c)
{
	/* Set up string to look for, e.g. "^d" */
	char inscrip[] = {'^', c, 0};

	for (int i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (obj != NULL) {
			unsigned checks = check_for_inscrip(obj, inscrip);

			while (checks > 0) {
				if (!get_check("Are you sure? ")) {
					return false;
				}
				checks--;
			}
		}
	}

	return true;
}

/**
 * Process a textui keypress.
 */
bool textui_process_key(struct keypress key, unsigned char *c)
{
	if (key.code == '\0'
			|| key.code == ESCAPE
			|| key.code == ' '
			|| key.code == '\a')
	{
		return true;
	}

	if (key.code > UCHAR_MAX) {
		return false;
	}

	*c = key.code;

	return true;
}
