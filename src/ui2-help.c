/**
 * \file ui2-help.c
 * \brief In-game help
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
#include "buildid.h"
#include "init.h"
#include "ui2-help.h"
#include "ui2-input.h"
#include "ui2-output.h"
#include "ui2-term.h"

/* 80 characters, +1 for null byte */
#define HELP_LINE_SIZE (80 + 1)
/* 80 characters, +2 for padding (left and right) */
#define HELP_TERM_WIDTH (80 + 2)
#define HELP_N_LINES 1024
#define HELP_MAX_MENU_FILES 26

struct help_line {
	char line[HELP_LINE_SIZE];
	/* Lowercase line, for use with case-insensitive search */
	char line_lc[HELP_LINE_SIZE];
};

struct help_file {
	struct help_line *lines;

	int line; /* current line */
	int next; /* one past the last line */
	int size; /* total number of allocated lines */

	int scroll;

	char search[HELP_LINE_SIZE];
	bool highlight;

	char *menu_files[HELP_MAX_MENU_FILES];
	bool menu;

	char caption[HELP_LINE_SIZE];

	char *name;
	char *tag;

	ang_file *file;
};

static void string_lower(char *buf)
{
	for (char *s = buf; *s != 0; s++) {
		*s = tolower((unsigned char) *s);
	}
}

static struct help_file *get_help_file(void)
{
	struct help_file *help = mem_zalloc(sizeof(*help));

	help->size = HELP_N_LINES;
	help->lines = mem_zalloc(help->size * sizeof(*help->lines));
	
	/* Pedantry */
	help->name    = NULL;
	help->tag     = NULL;
	help->file    = NULL;

	for (size_t i = 0; i < N_ELEMENTS(help->menu_files); i++) {
		help->menu_files[i] = NULL;
	}

	return help;
}

static void free_help_file(struct help_file *help)
{
	if (help->tag != NULL) {
		mem_free(help->tag);
	}
	if (help->name != NULL) {
		mem_free(help->name);
	}
	for (size_t i = 0; i < N_ELEMENTS(help->menu_files); i++) {
		if (help->menu_files[i] != NULL) {
			mem_free(help->menu_files[i]);
		}
	}
	mem_free(help->lines);
	mem_free(help);
}

static bool parse_help_line(struct help_file *help, const char *line)
{
	if (!prefix(line, ".. ")) {
		return false;
	}

	char buf[1024];
	char key;

	if (sscanf(line, ".. menu:: [%c] %1023s", &key, buf) == 2) {
		int index = A2I(key);
		if (index >= 0 && index < (int) N_ELEMENTS(help->menu_files)) {
			assert(help->menu_files[index] == NULL);

			help->menu_files[index] = string_make(buf);
			help->menu = true;
		}
	} else if (help->tag && sscanf(line, ".. _%1023[^:]%c", buf, &key) == 2) {
		if (key == ':' && streq(buf, help->tag)) {
			help->line = help->next; /* start with the tagged line (the next one) */
		}
	}

	do {
		file_getl(help->file, buf, sizeof(buf));
	} while (!contains_only_spaces(buf));

	return true;
}

static void slurp_help_file(struct help_file *help)
{
	assert(help->size > 0);
	assert(help->next >= 0);
	assert(help->next < help->size);

	char buf[HELP_LINE_SIZE];

	while (file_getl(help->file, buf, sizeof(buf))) {
		if (!parse_help_line(help, buf)) {

			if (help->next == help->size) {
				help->size *= 2;
				help->lines = mem_realloc(help->lines, help->size * sizeof(*help->lines));
			}

			strskip(buf, '|', '\\');
			strescape(buf, '\\');

			char *line = help->lines[help->next].line;
			size_t size = sizeof(help->lines[help->next].line);

			my_strcpy(line, buf, size);

			char *line_lc = help->lines[help->next].line_lc;
			size_t size_lc = sizeof(help->lines[help->next].line_lc);

			my_strcpy(line_lc, buf, size_lc);
			string_lower(line_lc);

			help->next++;
		}
	}
}

static void split_help_file_name(struct help_file *help, const char *name)
{
	assert(help->name == NULL);
	assert(help->tag == NULL);

	help->name = string_make(name);

	char *hash = strchr(help->name, '#');
	if (hash != NULL) {
		*hash = 0;
		help->tag = string_make(hash + 1);
	}
}

static struct help_file *open_help_file(const char *name)
{
	struct {const char *dir; const char *fmt;} dirs[] = {
		{ANGBAND_DIR_HELP, "%s, help file \"%s\""},
		{ANGBAND_DIR_INFO, "%s, user info file \"%s\""}
	};

	struct help_file *help = get_help_file();

	split_help_file_name(help, name);

	for (size_t i = 0; i < N_ELEMENTS(dirs) && help->file == NULL; i++) {
		char path[4096];
		path_build(path, sizeof(path), dirs[i].dir, help->name);

		help->file = file_open(path, MODE_READ, FTYPE_TEXT);
		if (help->file != NULL) {
			strnfmt(help->caption, sizeof(help->caption),
					dirs[i].fmt, buildid, help->name);
		}
	}

	if (help->file == NULL) {
		msg("Cannot open \"%s\".", name);
		event_signal(EVENT_MESSAGE_FLUSH);
		free_help_file(help);
		return NULL;
	} else {
		slurp_help_file(help);
		return help;
	}
}

static void close_help_file(struct help_file *help)
{
	file_close(help->file);
	free_help_file(help);
}

static void help_goto_file(const struct help_file *help)
{
	char name[HELP_LINE_SIZE];
	my_strcpy(name, help->name, sizeof(name));

	if (askfor_popup("View file: ", name, sizeof(name),
				ANGBAND_TERM_TEXTBLOCK_WIDTH / 2, TERM_POSITION_CENTER,
				NULL, NULL))
	{
		clear_prompt();

		Term_visible(false);
		show_help(name);
		Term_visible(true);
	}
}

static void try_show_help(const struct help_file *help, char key)
{
	if (help->menu && isalpha((unsigned char) key)) {
		int index = A2I(key);
		if (index >= 0
				&& index < (int) N_ELEMENTS(help->menu_files)
				&& help->menu_files[index] != NULL)
		{
			Term_visible(false);
			show_help(help->menu_files[index]);
			Term_visible(true);
		}
	}
}

static void help_goto_line(struct help_file *help)
{
	char line[4];
	my_strcpy(line, format("%zu", help->line), sizeof(line));

	const char *prompt = "Jump to line: ";

	if (askfor_popup(prompt, line, sizeof(line),
				strlen(prompt) + sizeof(line) - 1,
				TERM_POSITION_CENTER, NULL, askfor_numbers))
	{
		char *end = NULL;
		long l = strtol(line, &end, 10);
		if (line != end) {
			help->line = l - 1;
		}
	}

	clear_prompt();
}

static void help_set_regions(region *term_reg, region *text_reg)
{
	term_reg->x = 0;
	term_reg->y = 0;
	Term_get_size(&term_reg->w, &term_reg->h);

	text_reg->x = 1;
	text_reg->y = 2;
	text_reg->w = term_reg->w - 1;
	text_reg->h = term_reg->h - 4;

	assert(text_reg->h > 0);
}

static void help_display_frame(const struct help_file *help,
		region term_reg, region text_reg)
{
	struct loc loc;

	char line[ANGBAND_TERM_STANDARD_WIDTH / 2];
	strnfmt(line, sizeof(line), "[Line %d-%d/%d]",
			help->line + 1, help->line + text_reg.h, help->next);

	loc.x = term_reg.x;
	loc.y = term_reg.y;

	prt(line, loc);

	loc.x = term_reg.x;
	loc.y = term_reg.y + term_reg.h - 1;

	if (help->menu) {
		prt("[Press a letter to view other files, or ESC to exit.]", loc);
	} else {
		prt("[Press ESC to exit.]", loc);
	}
}

static void help_warn(const char *warning, region reg)
{
	const int y = reg.y + reg.h - 1;

	Term_erase_line(reg.x, y);
	Term_adds(reg.x, y, TERM_MAX_LEN, COLOUR_ORANGE, warning);
	Term_puts(TERM_MAX_LEN, COLOUR_ORANGE, " (any key to continue)");
	Term_flush_output();

	inkey_any();
}

static void help_find_line(struct help_file *help, region reg)
{
	Term_adds(reg.x, reg.y + reg.h - 1, TERM_MAX_LEN, COLOUR_WHITE, "Find: ");

	if (askfor_simple(help->search, sizeof(help->search), NULL)
			&& strlen(help->search) > 0)
	{
		bool found = false;

		string_lower(help->search);

		for (int n = 0, l = help->line + 1; !found && n < help->next; n++, l++) {
			if (l == help->next) {
				l = 0;
			}

			if (strstr(help->lines[l].line_lc, help->search)) {
				help->line = l;
				help->highlight = true;
				found = true;
			}
		}

		if (!found) {
			help_warn("Search string not found!", reg);
			memset(&help->search, 0, sizeof(help->search));
		}
	}

	Term_flush_output();
}

static void help_display_check(struct help_file *help, region reg)
{
	if (help->next > reg.h) {
		const int scroll_line = help->line + help->scroll;
		const int end_line = help->next - reg.h;

		assert(help->line <= end_line);

		if (scroll_line > end_line) {
			help->scroll = end_line - help->line;
		}
		if (scroll_line < 0) {
			help->scroll = -help->line;
		}
	} else {
		help->scroll = 0;
	}
}

static void help_scroll(struct help_file *help,
		region reg, int *line, int *row, int *end)
{
	const int abs_scroll = ABS(help->scroll);

	assert(abs_scroll != 0);
	assert(abs_scroll < reg.h);

	const struct loc src = {
		.x = reg.x,
		.y = help->scroll > 0 ? reg.y + abs_scroll: reg.y
	};
	const struct loc dst = {
		.x = reg.x,
		.y = help->scroll > 0 ? reg.y : reg.y + abs_scroll
	};

	const int height = reg.h - abs_scroll;

	Term_move_points(dst.x, dst.y, src.x, src.y, reg.w, height);

	help->line += help->scroll;

	if (help->scroll > 0) {
		*line += reg.h;
		*row += height;
	} else {
		*line -= abs_scroll;
		*end -= height;
	}

	help->scroll = 0;
}

static void help_display_page(struct help_file *help, region reg)
{
	help_display_check(help, reg);

	int line = help->line;
	int row  = reg.y;
	int end  = reg.y + reg.h;

	if (help->scroll != 0) {
		help_scroll(help, reg, &line, &row, &end);
	}

	while (row < end && line < help->next) {
		const struct help_line *hline = &help->lines[line];

		Term_erase(reg.x, row, reg.w);

		if (hline->line[0] != 0) {
			Term_adds(reg.x, row, reg.w, COLOUR_WHITE, hline->line);

			if (help->highlight) {
				size_t slen = strlen(help->search);

				if (slen > 0) {
					for (const char *found = strstr(hline->line_lc, help->search);
							found != NULL;
							found = strstr(found + slen, help->search))
					{
						ptrdiff_t pos = found - hline->line_lc;
						Term_adds(reg.x + pos, row, slen,
								COLOUR_YELLOW, hline->line + pos);
					}
				}
			}
		}

		line++;
		row++;
	}
}

static void show_file(const char *name)
{
	struct help_file *help = open_help_file(name);

	if (help == NULL) {
		return;
	}

	Term_add_tab(0, help->caption, COLOUR_WHITE, COLOUR_DARK);

	region term_reg;
	region text_reg;
	help_set_regions(&term_reg, &text_reg);

	const int end_line = help->next - text_reg.h;

	bool display = true;
	bool done = false;

	while (!done) {
		if (display) {
			help_display_page(help, text_reg);
			help_display_frame(help, term_reg, text_reg);
		}

		display = false;
		Term_flush_output();

		struct keypress key = inkey_only_key();

		switch (key.code) {
			case '?': case ESCAPE:
				done = true;
				break;
			case '&':
				help->highlight = !help->highlight;
				display = true;
				break;
			case '/':
				help_find_line(help, term_reg);
				display = true;
				break;
			case '#':
				help_goto_line(help);
				display = true;
				break;
			case '%':
				help_goto_file(help);
				break;
			case ARROW_UP: case '8': case '=': 
				if (help->line > 0) {
					help->scroll = -1;
					display = true;
				}
				break;
			case ARROW_DOWN: case '2': case KC_ENTER: 
				if (help->line < end_line) {
					help->scroll = 1;
					display = true;
				}
				break;
			case '_':
				if (help->line > 0) {
					help->scroll = -text_reg.h / 2;
					display = true;
				}
				break;
			case '+':
				if (help->line < end_line) {
					help->scroll = text_reg.h / 2;
					display = true;
				}
				break;
			case KC_PGUP: case '-':
				if (help->line > 0) {
					help->scroll = -(text_reg.h - 2);
					display = true;
				}
				break;
			case KC_PGDOWN: case ' ':
				if (help->line < end_line) {
					help->scroll = text_reg.h - 2;
					display = true;
				}
				break;
			default:
				try_show_help(help, key.code);
				break;
		}
	}

	close_help_file(help);
}

void show_help(const char *name)
{
	struct term_hints hints = {
		.width    = HELP_TERM_WIDTH,
		.height   = ANGBAND_TERM_STANDARD_HEIGHT,
		.tabs = true,
		.position = TERM_POSITION_CENTER,
		.purpose  = TERM_PURPOSE_TEXT
	};
	Term_push_new(&hints);

	show_file(name);

	Term_pop();
}

/**
 * Peruse the on-line help
 */
void do_cmd_help(void)
{
	show_help("help.hlp");
}
