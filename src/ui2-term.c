/**
 * \file ui2-term.c
 * \brief A generic, efficient, terminal window package
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

#include "ui2-term.h"
#include "z-virt.h"
#include "z-util.h"

/* Local declarations */

struct term_cursor {
	int x;
	int y;
	int i;
	bool visible;
};

struct term_dirty {
	int top;
	int bottom;
	struct {
		int left;
		int right;
	} *rows;

	bool *points;
};

struct term {
	void *user;

	bool temporary;

	struct term_point *points;

	int size;
	int width;
	int height;

	bool erased;

	struct {
		struct term_cursor new;
		struct term_cursor old;
	} cursor;

	struct term_dirty dirty;
	struct term_callbacks callbacks;
	struct term_point blank;
};

#define TERM_EVENT_QUEUE_MAX \
	1024

#define WIDESTRING_MAX \
	1024

#define TOP \
	(term_stack.stack[term_stack.top])

#define NOTOP \
	(-1)

#define COORDS_OK(x, y) do { \
	assert(TOP != NULL); \
	assert((x) >= 0); \
	assert((x) < TOP->width); \
	assert((y) >= 0); \
	assert((y) < TOP->height); \
} while (0)

#define INDEX_OK(index) do { \
	assert((index) >= 0); \
	assert((index) < TOP->size); \
} while (0) 

#define STACK_OK() do { \
	assert(TOP != NULL); \
	assert(term_stack.top > NOTOP); \
	assert(term_stack.top < (int) N_ELEMENTS(term_stack.stack)); \
} while (0)

/* cursor's x position can be one point beyound the right edge of the term */
#define CURSOR_OK() do { \
	assert(TOP->cursor.new.x >= 0); \
	assert(TOP->cursor.new.x <= TOP->width); \
	assert(TOP->cursor.new.y >= 0); \
	assert(TOP->cursor.new.y < TOP->height); \
	assert(TOP->cursor.new.i >= 0); \
	assert(TOP->cursor.new.i <= TOP->size); \
} while (0)

#define INDEX(x, y) \
	((x) + (y) * TOP->width)

/* Local variables */

static struct {
	term stack[TERM_STACK_MAX];
	int top;
} term_stack = {
	.top = NOTOP
};

static struct {
	ui_event queue[TERM_EVENT_QUEUE_MAX];
	size_t head;
	size_t tail;
	size_t size;
	size_t number;
} event_queue = {
	.size = TERM_EVENT_QUEUE_MAX
};

/* Local functions */

static term term_alloc(int w, int h)
{
	assert(w > 0);
	assert(h > 0);

	struct term *t = mem_zalloc(sizeof(*t));

	t->width = w;
	t->height = h;
	t->size = w * h;

	t->points = mem_zalloc(t->size * sizeof(t->points[0]));
	t->dirty.rows = mem_zalloc(t->height * sizeof(t->dirty.rows[0]));
	t->dirty.points = mem_zalloc(t->size * sizeof(t->dirty.points[0]));

	return t;
}

static void term_free(term t)
{
	mem_free(t->points);
	mem_free(t->dirty.rows);
	mem_free(t->dirty.points);
	mem_free(t);
}

static term term_new(const struct term_create_info *info)
{
	term t = term_alloc(info->width, info->height);

	t->user = info->user;
	t->blank = info->blank;
	t->callbacks = info->callbacks;

	assert(t->callbacks.make_visible != NULL);
	assert(t->callbacks.flush_events != NULL);
	assert(t->callbacks.push_new     != NULL);
	assert(t->callbacks.pop_new      != NULL);
	assert(t->callbacks.add_tab      != NULL);
	assert(t->callbacks.redraw       != NULL);
	assert(t->callbacks.cursor       != NULL);
	assert(t->callbacks.erase        != NULL);
	assert(t->callbacks.event        != NULL);
	assert(t->callbacks.delay        != NULL);
	assert(t->callbacks.draw         != NULL);

	for (int i = 0; i < t->size; i++) {
		t->points[i] = t->blank;
	}

	for (int y = 0; y < t->height; y++) {
		t->dirty.rows[y].left = t->width;
		t->dirty.rows[y].right = 0;
	}
	t->dirty.top = t->height;
	t->dirty.bottom = 0;

	t->temporary = false;
	
	return t;
}

static void term_draw(int x, int y, int index, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	assert(len > 0);

	INDEX_OK(index + len - 1);

	TOP->callbacks.draw(TOP->user, x, y, len, &TOP->points[index]);
}

static bool term_points_equal(struct term_point original, struct term_point compare)
{
	return original.fg_attr == compare.fg_attr
		&& original.fg_char == compare.fg_char
		&& original.bg_attr == compare.bg_attr
		&& original.bg_char == compare.bg_char
		&& original.terrain_attr == compare.terrain_attr
		&& tpf_is_equal(original.flags, compare.flags);
}

static bool term_fg_equal(struct term_point original, uint32_t fga, wchar_t fgc)
{
	return original.fg_attr == fga
		&& original.fg_char == fgc;
}

static void term_mark_point_dirty(int x, int y, int index)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	if (!TOP->dirty.points[index]) {

		if (y < TOP->dirty.top) {
			TOP->dirty.top = y;
		}
		if (y > TOP->dirty.bottom) {
			TOP->dirty.bottom = y;
		}

		if (x < TOP->dirty.rows[y].left) {
			TOP->dirty.rows[y].left = x;
		}
		if (x > TOP->dirty.rows[y].right) {
			TOP->dirty.rows[y].right = x;
		}

		TOP->erased = false;
		TOP->dirty.points[index] = true;
	}
}

static void term_mark_row_flushed(int y)
{
	STACK_OK();

	TOP->dirty.rows[y].left = TOP->width;
	TOP->dirty.rows[y].right = 0;
}

static void term_mark_all_flushed(void)
{
	STACK_OK();

	TOP->dirty.top = TOP->height;
	TOP->dirty.bottom = 0;
}

static struct term_point term_get_point(int index)
{
	STACK_OK();
	INDEX_OK(index);
	
	return TOP->points[index];
}

static void term_set_point(int x, int y, int index, struct term_point point)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	if (!term_points_equal(TOP->points[index], point)) {
		TOP->points[index] = point;
		term_mark_point_dirty(x, y, index);
	}
}

static void term_set_fg(int x, int y, int index, uint32_t fga, wchar_t fgc)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	if (!term_fg_equal(TOP->points[index], fga, fgc)) {
		TOP->points[index].fg_attr = fga;
		TOP->points[index].fg_char = fgc;

		term_mark_point_dirty(x, y, index);
	}
}

static int term_set_ws(int x, int y, int index, int len,
		uint32_t fga, const wchar_t *ws)
{
	STACK_OK();
	COORDS_OK(x, y);

	if (len == TERM_MAX_LEN) {
		len = TOP->width;
	}

	assert(len > 0);
	assert(*ws != 0);

	const wchar_t *curws = ws;

	for (int z = MIN(TOP->width, x + len);
			x < z && *curws != 0;
			x++, index++, curws++)
	{
		term_set_fg(x, y, index, fga, *curws);
	}

	return curws - ws;
}

static bool term_put_point_at_cursor(struct term_point point)
{
	STACK_OK();
	CURSOR_OK();
	
	if (TOP->cursor.new.x < TOP->width) {
		term_set_point(TOP->cursor.new.x, TOP->cursor.new.y,
				TOP->cursor.new.i, point);
		TOP->cursor.new.x++;
		TOP->cursor.new.i++;
		return true;
	} else {
		return false;
	}
}

static bool term_put_fg_at_cursor(uint32_t fga, wchar_t fgc)
{
	STACK_OK();
	CURSOR_OK();
	
	if (TOP->cursor.new.x < TOP->width) {
		term_set_fg(TOP->cursor.new.x, TOP->cursor.new.y,
				TOP->cursor.new.i, fga, fgc);
		TOP->cursor.new.x++;
		TOP->cursor.new.i++;
		return true;
	} else {
		return false;
	}
}

static bool term_put_ws_at_cursor(int len, uint32_t fga, const wchar_t *ws)
{
	STACK_OK();
	CURSOR_OK();

	int add = term_set_ws(TOP->cursor.new.x, TOP->cursor.new.y,
			TOP->cursor.new.i, len, fga, ws);

	TOP->cursor.new.x += add;
	TOP->cursor.new.i += add;

	CURSOR_OK();

	return TOP->cursor.new.x < TOP->width;
}

static void term_wipe_line(int x, int y, int index, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	if (len == TERM_MAX_LEN) {
		len = TOP->width - x;
	}

	assert(len > 0);

	for (int z = MIN(x + len, TOP->width); x < z; x++, index++) {

		if (!term_points_equal(TOP->points[index], TOP->blank)) {
			TOP->points[index] = TOP->blank;
			term_mark_point_dirty(x, y, index);
		}
	}
}

static void term_wipe_all(void)
{
	STACK_OK();

	TOP->callbacks.erase(TOP->user);

	for (int i = 0; i < TOP->size; i++) {
		TOP->points[i] = TOP->blank;
		TOP->dirty.points[i] = false;
	}

	for (int y = 0; y < TOP->height; y++) {
		term_mark_row_flushed(y);
	}
	term_mark_all_flushed();

	TOP->erased = true;
}

static void term_move_cursor(int x, int y, int index)
{
	STACK_OK();
	CURSOR_OK();

	TOP->cursor.new.x = x;
	TOP->cursor.new.y = y;
	TOP->cursor.new.i = index;

	CURSOR_OK();
}

static bool term_check_event(ui_event *event)
{
	if (event_queue.number == 0) {
		return false;
	}

	if (event != NULL) {
		*event = event_queue.queue[event_queue.tail];
	}

	return true;
}

static bool term_take_event(ui_event *event)
{
	if (!term_check_event(event)) {
		return false;
	}

	assert(event_queue.number > 0);

	event_queue.number--;
	event_queue.tail++;

	if (event_queue.tail == event_queue.size) {
		event_queue.tail = 0;
	}

	return true;
}

static bool term_prepend_events(const ui_event *events, size_t num_events)
{
	if (event_queue.size - event_queue.number < num_events) {
		return false;
	}

	for (size_t i = num_events; i > 0; i--) {
		if (event_queue.tail == 0) {
			event_queue.tail = event_queue.size;
		}
		event_queue.tail--;

		event_queue.queue[event_queue.tail] = events[i - 1];
		event_queue.number++;
	}

	return true;
}

static bool term_append_events(const ui_event *events, size_t num_events)
{
	if (event_queue.size - event_queue.number < num_events) {
		return false;
	}

	for (size_t i = 0; i < num_events; i++) {
		event_queue.queue[event_queue.head] = events[i];
		event_queue.number++;

		event_queue.head++;
		if (event_queue.head == event_queue.size) {
			event_queue.head = 0;
		}
	}

	return true;
}

static void term_mbstowcs(wchar_t *ws, const char *mbs, size_t ws_max_len)
{
	size_t len = text_mbstowcs(ws, mbs, ws_max_len);
	assert(len != (size_t) -1);
	ws[ws_max_len - 1] = 0;
}

static void term_erase_cursor(struct term_cursor cursor)
{
	STACK_OK();

	if (cursor.visible && cursor.x < TOP->width) {
		term_draw(cursor.x, cursor.y, cursor.i, 1);
	}
}

static void term_draw_cursor(struct term_cursor cursor)
{
	STACK_OK();

	if (cursor.visible && cursor.x < TOP->width) {
		TOP->callbacks.cursor(TOP->user, cursor.x, cursor.y);
	}
}

static void term_flush_pending(int x, int y, int index, int len)
{
	STACK_OK();
	COORDS_OK(x, y);
	INDEX_OK(index);

	assert(len > 0);

	int pending_x = 0;
	int pending_i = 0;
	int pending_len = 0;

	for (int z = x + len; x < z; x++, index++) {

		if (TOP->dirty.points[index]) {
			if (pending_len == 0) {
				pending_x = x;
				pending_i = index;
			}
			pending_len++;

		} else {
			if (pending_len > 0) {
				term_draw(pending_x, y, pending_i, pending_len);
				pending_len = 0;
			}
		}

		TOP->dirty.points[index] = false;
	}

	if (pending_len > 0) {
		term_draw(pending_x, y, pending_i, pending_len);
	}
}

static void term_flush_row(int y)
{
	STACK_OK();

	const int left = TOP->dirty.rows[y].left;
	const int right = TOP->dirty.rows[y].right;

	int len = right - left + 1;

	if (len > 0) {
		term_mark_row_flushed(y);

		term_flush_pending(left, y, INDEX(left, y), len);
	}
}

static void term_flush_out(void)
{
	STACK_OK();

	const int top = TOP->dirty.top;
	const int bottom = TOP->dirty.bottom;

	term_mark_all_flushed();

	for (int y = top; y <= bottom; y++) {
		term_flush_row(y);
	}
}

static void term_pop_stack(void)
{
	STACK_OK();

	if (TOP->temporary) {
		TOP->callbacks.pop_new(TOP->user);
		term_free(TOP);
	}

	TOP = NULL;
	term_stack.top--;
}

/* External functions */

term Term_create(const struct term_create_info *info)
{
	return term_new(info);
}

void Term_destroy(term t)
{
	assert(t->temporary == false);

	term_free(t);
}

void Term_setpriv(term t, void *user)
{
	t->user = user;
}

void *Term_priv(term t)
{
	return t->user;
}

term Term_top(void)
{
	if (term_stack.top > NOTOP) {
		return TOP;
	} else {
		return NULL;
	}
}

void Term_push(term t)
{
	assert(t != NULL);
	assert(!t->temporary);

	term_stack.top++;
	TOP = t;

	STACK_OK();
}

void Term_push_new(struct term_hints *hints)
{
	assert(hints->width > 0);
	assert(hints->height > 0);

	struct term_create_info info = {0};

	TOP->callbacks.push_new(hints, &info);

	if (info.width == 0) {
		info.width = hints->width;
	}
	if (info.height == 0) {
		info.height = hints->height;
	}

	term_stack.top++;
	TOP = term_new(&info);
	TOP->temporary = true;

	STACK_OK();
}

void Term_pop(void)
{
	term_pop_stack();
}

void Term_pop_all(void)
{
	while (term_stack.top > NOTOP) {
		term_pop_stack();
	}
}

bool Term_putwc(uint32_t fga, wchar_t fgc)
{
	return term_put_fg_at_cursor(fga, fgc);
}

bool Term_putws(int len, uint32_t fga, const wchar_t *fgc)
{
	return term_put_ws_at_cursor(len, fga, fgc);
}

bool Term_puts(int len, uint32_t fga, const char *fgc)
{
	wchar_t ws[WIDESTRING_MAX];
	term_mbstowcs(ws, fgc, WIDESTRING_MAX);

	return term_put_ws_at_cursor(len, fga, ws);
}

bool Term_addwc(int x, int y, uint32_t fga, wchar_t fgc)
{
	term_move_cursor(x, y, INDEX(x, y));

	return term_put_fg_at_cursor(fga, fgc);
}

bool Term_addws(int x, int y, int len, uint32_t fga, const wchar_t *fgc)
{
	term_move_cursor(x, y, INDEX(x, y));

	return term_put_ws_at_cursor(len, fga, fgc);
}

bool Term_adds(int x, int y, int len, uint32_t fga, const char *fgc)
{
	term_move_cursor(x, y, INDEX(x, y));

	wchar_t ws[WIDESTRING_MAX];
	term_mbstowcs(ws, fgc, WIDESTRING_MAX);

	return term_put_ws_at_cursor(len, fga, ws);
}

void Term_add_tab(keycode_t code,
		const char *label, uint32_t fg_attr, uint32_t bg_attr)
{
	STACK_OK();

	wchar_t ws[WIDESTRING_MAX];
	term_mbstowcs(ws, label, WIDESTRING_MAX);

	TOP->callbacks.add_tab(TOP->user, code, ws, fg_attr, bg_attr);
}

void Term_erase(int x, int y, int len)
{
	STACK_OK();

	term_wipe_line(x, y, INDEX(x, y), len);
}

void Term_erase_line(int x, int y)
{
	STACK_OK();

	term_wipe_line(x, y, INDEX(x, y), TOP->width);
}

void Term_erase_all(void)
{
	STACK_OK();

	if (!TOP->erased) {
		term_wipe_all();
	}
}

void Term_dirty_point(int x, int y)
{
	STACK_OK();
	COORDS_OK(x, y);

	term_mark_point_dirty(x, y, INDEX(x, y));
}

void Term_get_cursor(int *x, int *y, bool *visible)
{
	STACK_OK();
	CURSOR_OK();

	if (x != NULL) {
		*x = TOP->cursor.new.x;
	}
	if (y != NULL) {
		*y = TOP->cursor.new.y;
	}
	if (visible != NULL) {
		*visible = TOP->cursor.new.visible;
	}
}

void Term_cursor_to_xy(int x, int y)
{
	STACK_OK();

	term_move_cursor(x, y, INDEX(x, y));
}

void Term_cursor_visible(bool visible)
{
	STACK_OK();

	TOP->cursor.new.visible = visible;
}

void Term_visible(bool visible)
{
	STACK_OK();

	TOP->callbacks.make_visible(TOP->user, visible);
}

void Term_get_size(int *w, int *h)
{
	STACK_OK();

	*w = TOP->width;
	*h = TOP->height;
}

int Term_width(void)
{
	STACK_OK();

	return TOP->width;
}

int Term_height(void)
{
	STACK_OK();

	return TOP->height;
}

void Term_get_point(int x, int y, struct term_point *point)
{
	STACK_OK();
	COORDS_OK(x, y);

	*point = term_get_point(INDEX(x, y));
}

struct term_point Term_get_blank(void)
{
	STACK_OK();

	return TOP->blank;
}

void Term_set_point(int x, int y, struct term_point point)
{
	STACK_OK();

	term_set_point(x, y, INDEX(x, y), point);
}

void Term_add_point(int x, int y, struct term_point point)
{
	STACK_OK();

	term_move_cursor(x, y, INDEX(x, y));
	term_put_point_at_cursor(point);
}

bool Term_point_ok(int x, int y)
{
	STACK_OK();

	return x >= 0
		&& y >= 0
		&& x < TOP->width
		&& y < TOP->height;
}

void Term_resize(int w, int h)
{
	STACK_OK();

	assert(w > 0);
	assert(h > 0);

	struct term old = *TOP;

	TOP->width = w;
	TOP->height = h;
	TOP->size = w * h;

	TOP->points = mem_zalloc(TOP->size * sizeof(TOP->points[0]));

	TOP->dirty.rows = mem_zalloc(TOP->height * sizeof(TOP->dirty.rows[0]));
	TOP->dirty.points = mem_zalloc(TOP->size * sizeof(TOP->dirty.points[0]));
	TOP->dirty.top = w;
	TOP->dirty.bottom = 0;

	TOP->cursor.new.x = 0;
	TOP->cursor.new.y = 0;
	TOP->cursor.new.i = 0;
	TOP->cursor.old.x = 0;
	TOP->cursor.old.y = 0;
	TOP->cursor.old.i = 0;

	term_wipe_all();

	mem_free(old.points);
	mem_free(old.dirty.rows);
	mem_free(old.dirty.points);
}

void Term_flush_output(void)
{
	STACK_OK();

	term_erase_cursor(TOP->cursor.old);
	term_flush_out();
	term_draw_cursor(TOP->cursor.new);

	TOP->cursor.old = TOP->cursor.new;
}

void Term_redraw_screen(void)
{
	STACK_OK();

	TOP->callbacks.redraw(TOP->user);
}

bool Term_keypress(keycode_t key, byte mods)
{
	STACK_OK();

	ui_event event = {
		.key = {
			.type = EVT_KBRD,
			.code = key,
			.mods = mods
		}
	};

	return term_append_events(&event, 1);
}

bool Term_mousepress(int x, int y, int button, byte mods, int index)
{
	STACK_OK();

	ui_event event = {
		.mouse = {
			.type = EVT_MOUSE,
			.x = x,
			.y = y,
			.button = button,
			.mods = mods,
			.index = index
		}
	};

	return term_append_events(&event, 1);
}

bool Term_take_event(ui_event *event)
{
	STACK_OK();

	if (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, false);
	}

	return term_take_event(event);
}

bool Term_wait_event(ui_event *event)
{
	STACK_OK();

	assert(event != NULL);

	while (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, true);
	}

	return term_take_event(event);
}

bool Term_check_event(ui_event *event)
{
	STACK_OK();

	if (event_queue.number == 0) {
		TOP->callbacks.event(TOP->user, false);
	}

	return term_check_event(event);
}

bool Term_prepend_events(const ui_event *events, size_t num_events)
{
	return term_prepend_events(events, num_events);
}

bool Term_append_events(const ui_event *events, size_t num_events)
{
	return term_append_events(events, num_events);
}

void Term_flush_events(void)
{
	STACK_OK();

	TOP->callbacks.flush_events(TOP->user);

	event_queue.head = 0;
	event_queue.tail = 0;
	event_queue.number = 0;
}

void Term_delay(int msecs)
{
	STACK_OK();

	assert(msecs > 0);

	TOP->callbacks.delay(TOP->user, msecs);
}
