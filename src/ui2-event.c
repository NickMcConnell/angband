/**
 * \file ui2-event.c
 * \brief Utility functions relating to UI events
 *
 * Copyright (c) 2011 Andi Sidwell
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
#include "ui2-display.h"
#include "ui2-event.h"

/**
 * Map keycodes to their textual equivalent.
 */
static const struct {
	keycode_t code;
	const char *desc;
} mappings[] = {
	{ESCAPE,       "Escape"},
	{KC_ENTER,     "Enter"},
	{KC_TAB,       "Tab"},
	{KC_DELETE,    "Delete"},
	{KC_BACKSPACE, "Backspace"},
	{ARROW_DOWN,   "Down"},
	{ARROW_LEFT,   "Left"},
	{ARROW_RIGHT,  "Right"},
	{ARROW_UP,     "Up"},
	{KC_F1,        "F1"},
	{KC_F2,        "F2"},
	{KC_F3,        "F3"},
	{KC_F4,        "F4"},
	{KC_F5,        "F5"},
	{KC_F6,        "F6"},
	{KC_F7,        "F7"},
	{KC_F8,        "F8"},
	{KC_F9,        "F9"},
	{KC_F10,       "F10"},
	{KC_F11,       "F11"},
	{KC_F12,       "F12"},
	{KC_F13,       "F13"},
	{KC_F14,       "F14"},
	{KC_F15,       "F15"},
	{KC_HELP,      "Help"},
	{KC_HOME,      "Home"},
	{KC_PGUP,      "PageUp"},
	{KC_END,       "End"},
	{KC_PGDOWN,    "PageDown"},
	{KC_INSERT,    "Insert"},
	{KC_PAUSE,     "Pause"},
	{KC_BREAK,     "Break"},
	{KC_BEGIN,     "Begin"},
};

/**
 * Given a string, try and find it in mappings[].
 */
keycode_t keycode_find_code(const char *str, size_t len)
{
	for (size_t i = 0; i < N_ELEMENTS(mappings); i++) {
		if (strncmp(str, mappings[i].desc, len) == 0) {
			return mappings[i].code;
		}
	}

	return 0;
}

/**
 * Given a keycode, return its textual mapping.
 */
const char *keycode_find_desc(keycode_t code)
{
	for (size_t i = 0; i < N_ELEMENTS(mappings); i++) {
		if (mappings[i].code == code) {
			return mappings[i].desc;
		}
	}

	return NULL;
}

/**
 * Convert a hexidecimal-digit into a decimal
 */
static int dehex(char c)
{
	if (isdigit((unsigned char) c)) {
		return D2I(c);
	}
	if (isalpha((unsigned char) c)) {
		return A2I(tolower((unsigned char) c)) + 10;
	}

	return 0;
}

/**
 * Convert an encoding of a set of keypresses into actual keypresses.
 */
void keypress_from_text(struct keypress *buf, size_t len, const char *str)
{
	assert(len > 0);

	size_t cur = 0;
	byte mods = 0;

	memset(buf, 0, len * sizeof(*buf));

#define STORE(buffer, pos, mod, cod) do { \
	int p = (pos); \
	byte m = (mod); \
	keycode_t c = (cod); \
\
	if ((m & KC_MOD_CONTROL) && ENCODE_KTRL(c)) { \
		m &= ~KC_MOD_CONTROL; \
		c = KTRL(c); \
	} \
\
	buffer[p].mods = m; \
	buffer[p].code = c; \
} while (0)

	/* Analyze the ascii string */
	while (*str && cur < len) {
		buf[cur].type = EVT_KBRD;

		if (*str == '\\') {
			/* C-style escaped character */
			str++;
			if (*str == '\0') {
				break;
			}

			switch (*str) {
				/* Hex-mode */
				case 'x': {
					if (isxdigit((unsigned char) (*(str + 1)))
							&& isxdigit((unsigned char) (*(str + 2))))
					{
						int v1 = dehex(*++str) * 16;
						int v2 = dehex(*++str);
						/* store a nice hex digit */
						STORE(buf, cur++, mods, v1 + v2);
					} else {
						/* invalids get ignored */
						STORE(buf, cur++, mods, '?');
					}
					break;
				}

				case 'a': STORE(buf, cur++, mods, '\a'); break;
				default:  STORE(buf, cur++, mods, *str); break;
			}

			mods = 0;

			/* Skip the final char */
			str++;
		} else if (*str == '[') {
			/* Non-ascii keycodes */
			str++;

			char *end = strchr(str, (unsigned char) ']');
			if (end == NULL) {
				return;
			}

			keycode_t code = keycode_find_code(str, (size_t) (end - str));
			if (code == 0) {
				return;
			}

			STORE(buf, cur++, mods, code);
			mods = 0;
			str = end + 1;
		} else if (*str == '{') {
			/* Modifiers for next character */
			str++;
			if (!strchr(str, (unsigned char) '}')) {
				return;
			}

			/* Analyze modifier chars */
			while (*str != '}') {
				switch (*str) {
					case '^': mods |= KC_MOD_CONTROL; break;
					case 'S': mods |= KC_MOD_SHIFT;   break;
					case 'A': mods |= KC_MOD_ALT;     break;
					case 'M': mods |= KC_MOD_META;    break;
					case 'K': mods |= KC_MOD_KEYPAD;  break;
					default:
						return;
				}
				str++;
			}

			/* Skip ending bracket */
			str++;
		} else if (*str == '^') {
			/* Shorthand for Ctrl */
			str++;
			mods |= KC_MOD_CONTROL;
		} else {
			/* Everything else */
			STORE(buf, cur++, mods, *str++);
			mods = 0;
		}
	}

	/* Terminate */
	cur = MIN(cur, len - 1);
	buf[cur] = KEYPRESS_NULL;
}

/**
 * Convert a string of keypresses into their textual equivalent.
 */
void keypress_to_text(char *buf, size_t len,
		const struct keypress *src, bool expand_backslash)
{
	size_t cur = 0;
	size_t end = 0;

	while (src[cur].type == EVT_KBRD) {
		keycode_t code = src[cur].code;
		int mods = src[cur].mods;
		const char *desc = keycode_find_desc(code);

		/* UN_KTRL() control characters if they don't have a description
		 * this is so that Tab (^I) doesn't get turned into ^I but gets
		 * displayed as [Tab] */
		if (code < 0x20 && !desc) {
			mods |= KC_MOD_CONTROL;
			code = UN_KTRL(code);
		}

		if (mods) {
			if (mods & KC_MOD_CONTROL && !(mods & ~KC_MOD_CONTROL)) {
				strnfcat(buf, len, &end, "^");			
			} else {
				strnfcat(buf, len, &end, "{");
				if (mods & KC_MOD_CONTROL) strnfcat(buf, len, &end, "^");
				if (mods & KC_MOD_SHIFT)   strnfcat(buf, len, &end, "S");
				if (mods & KC_MOD_ALT)     strnfcat(buf, len, &end, "A");
				if (mods & KC_MOD_META)    strnfcat(buf, len, &end, "M");
				if (mods & KC_MOD_KEYPAD)  strnfcat(buf, len, &end, "K");
				strnfcat(buf, len, &end, "}");
			}
		}

		if (desc) {
			strnfcat(buf, len, &end, "[%s]", desc);
		} else {
			switch (code) {
				case '\a':
					strnfcat(buf, len, &end, "\\a");
					break;

				case '\\':
					if (expand_backslash) {
						strnfcat(buf, len, &end, "\\\\");
					} else {
						strnfcat(buf, len, &end, "\\");
					}
					break;

				case '^':
					strnfcat(buf, len, &end, "\\^");
					break;

				case '[':
					strnfcat(buf, len, &end, "\\[");
					break;

				default:
					if (code < 127) {
						strnfcat(buf, len, &end, "%c", (int) code);
					} else {
						strnfcat(buf, len, &end, "\\x%02x", (unsigned) code);
					}
					break;
			}
		}

		cur++;
	}

	assert(end < len);
	buf[end] = '\0';
}

/**
 * Convert a keypress into something readable.
 */
void keypress_to_readable(char *buf, size_t len, struct keypress src)
{
	size_t end = 0;

	keycode_t code = src.code;
	int mods = src.mods;
	const char *desc = keycode_find_desc(code);

	/* UN_KTRL() control characters if they don't have a description
	 * this is so that Tab (^I) doesn't get turned into ^I but gets
	 * displayed as [Tab] */
	if (code < 0x20 && !desc) {
		mods |= KC_MOD_CONTROL;
		code = UN_KTRL(code);
	}

	if (mods) {
		if (mods & KC_MOD_CONTROL
				&& !(mods & ~KC_MOD_CONTROL)
				&& code != '^')
		{
			strnfcat(buf, len, &end, "^");
		} else {
			if (mods & KC_MOD_CONTROL) strnfcat(buf, len, &end, "Control-");
			if (mods & KC_MOD_SHIFT)   strnfcat(buf, len, &end, "Shift-");
			if (mods & KC_MOD_ALT)     strnfcat(buf, len, &end, "Alt-");
			if (mods & KC_MOD_META)    strnfcat(buf, len, &end, "Meta-");
			if (mods & KC_MOD_KEYPAD)  strnfcat(buf, len, &end, "Keypad-");
		}
	}

	if (desc) {
		strnfcat(buf, len, &end, "%s", desc);
	} else {
		strnfcat(buf, len, &end, "%c", (int) code);
	}

	assert(end < len);
	buf[end] = '\0';
}

/**
 * Return whether the given display char matches an entered symbol
 */
bool char_matches_key(wchar_t wc, keycode_t key)
{
	char utf8[5];

	/* Assuming that key is a Unicode codepoint */

	if (key < 0x80) {
		utf8[0] = (char) key;
		utf8[1] = 0;
	} else if (key < 0x800) {
		utf8[0] = (char) (( key >>  6)         | 0xC0);
		utf8[1] = (char) (( key        & 0x3F) | 0x80);
		utf8[2] = 0;
    } else if (key < 0x10000) {
		utf8[0] = (char) (( key >> 12)         | 0xE0);
		utf8[1] = (char) (((key >>  6) & 0x3F) | 0x80);
		utf8[2] = (char) (( key        & 0x3F) | 0x80);
		utf8[3] = 0;
    } else if (key < 0x10FFFF) {
		utf8[0] = (char) (( key >> 18)         | 0xF0);
		utf8[1] = (char) (((key >> 12) & 0x3F) | 0x80);
		utf8[2] = (char) (((key >>  6) & 0x3F) | 0x80);
		utf8[3] = (char) (( key        & 0x3F) | 0x80);
		utf8[4] = 0;
	} else {
		return false;
	}

	wchar_t wchar;
	text_mbstowcs(&wchar, utf8, 1);

	return wc == wchar;
}

int event_grid_x(int x)
{
	struct loc coords;
	display_term_get_coords(DISPLAY_CAVE, &coords);

	return x + coords.x;
}

int event_grid_y(int y)
{
	struct loc coords;
	display_term_get_coords(DISPLAY_CAVE, &coords);

	return y + coords.y;
}
