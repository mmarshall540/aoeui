#include "all.h"
#include <dirent.h>
#ifndef NAME_MAX
# define NAME_MAX 256
#endif

unsigned find_line_start(struct view *view, unsigned offset)
{
	int ch;

	while ((ch = view_byte(view, --offset)) >= 0)
		if (ch == '\n')
			break;
	return offset+1;
}

unsigned find_line_end(struct view *view, unsigned offset)
{
	int ch;

	for (; (ch = view_byte(view, offset)) >= 0; offset++)
		if (ch == '\n')
			break;
	return offset;
}

unsigned find_word_start(struct view *view, unsigned offset)
{
	int ch;

	while ((ch = view_byte(view, --offset)) >= 0)
		if (!isspace(ch)) {
			offset--;
			break;
		}
	for (; (ch = view_byte(view, offset)) >= 0; offset--)
		if (!isalnum(ch))
			break;
	return offset+1;
}

unsigned find_word_end(struct view *view, unsigned offset)
{
	int ch;

	for (offset++; (ch = view_byte(view, offset)) >= 0; offset++)
		if (!isspace(ch))
			break;
	for (; (ch = view_byte(view, offset)) >= 0; offset++)
		if (!isalnum(ch))
			break;
	return offset;
}

unsigned find_sentence_start(struct view *view, unsigned offset)
{
	int ch, next = view_byte(view, --offset);

	if (next < 0)
		return offset;
	while ((ch = view_byte(view, --offset)) >= 0)
		if (ch == '.' || ch == ',' || ch == ';' || ch == ':' ||
		    ch == '(' || ch == '[' || ch == '{' ||
		    ch == '\n' && ch == next)
			break;
		else
			next = ch;
	return offset+1;
}

unsigned find_sentence_end(struct view *view, unsigned offset)
{
	int ch, last = -1;

	for (offset++; (ch = view_byte(view, offset)) >= 0; offset++)
		if (ch == '.' || ch == ',' || ch == ';' ||
		    ch == '!' || ch == '?' ||
		    ch == ')' || ch == ']' || ch == '}' ||
		    ch == '\n' && ch == last)
			break;
		else
			last = ch;
	return offset;
}

int view_unicode_slow(struct view *view, unsigned offset, unsigned *length)
{
	char *raw;
	*length = view_raw(view, &raw, offset, 8);
	*length = utf8_length(raw, *length);
	return utf8_unicode(raw, *length);
}

int view_unicode_prior(struct view *view, unsigned offset, unsigned *prev)
{
	int ch = -1;
	char *raw;

	if (offset) {
		ch = view_byte(view, --offset);
		if (ch >= 0x80) {
			unsigned at = offset >= 7 ? offset-7 : 0;
			view_raw(view, &raw, at, offset-at+1);
			offset -= utf8_length_backwards(raw+offset-at,
					offset-at+1) - 1;
			ch = view_unicode(view, offset, &at);
		}
	}
	if (prev)
		*prev = offset;
	return ch;
}

void view_erase(struct view *view)
{
	view_delete(view, 0, view->bytes);
}

int view_vprintf(struct view *view, const char *msg, va_list ap)
{
	char buff[1024];
	vsnprintf(buff, sizeof buff, msg, ap);
	return view_insert(view, buff, view->bytes, -1);
}

int view_printf(struct view *view, const char *msg, ...)
{
	va_list ap;
	int result;

	va_start(ap, msg);
	result = view_vprintf(view, msg, ap);
	va_end(ap);
	return result;
}

unsigned view_get_selection(struct view *view, unsigned *offset, int *append)
{
	unsigned cursor = locus_get(view, CURSOR);
	unsigned mark = locus_get(view, MARK);
	if (mark == UNSET)
		mark = cursor + (cursor < view->bytes);
	if (append)
		*append = cursor >= mark;
	if (mark <= cursor)
		return cursor - (*offset = mark);
	return mark - (*offset = cursor);
}

char *view_extract_selection(struct view *view)
{
	unsigned offset;
	unsigned bytes = view_get_selection(view, &offset, NULL);
	char *path;

	if (!bytes)
		return NULL;
	path = allocate(NULL, bytes+1);
	path[view_get(view, path, offset, bytes)] = '\0';
	return path;
}

unsigned view_delete_selection(struct view *view)
{
	unsigned offset;
	unsigned bytes = view_get_selection(view, &offset, NULL);
	view_delete(view, offset, bytes);
	locus_set(view, MARK, UNSET);
	return bytes;
}

struct view *view_next(struct view *view)
{
	struct view *new = view;
	do {
		if (new->next)
			new = new->next;
		else if (new->text->next)
			new = new->text->next->views;
		else
			new = text_list->views;
	} while (new != view && new->window);
	return new == view ? text_new() : new;
}

int view_corresponding_bracket(struct view *view, unsigned offset)
{
	static signed char peer[0x100], updown[0x100];

	int ch = view_byte(view, offset);
	unsigned char stack[32];
	int stackptr = 0;
	int dir;

	if (!peer['(']) {
		peer['('] = ')', peer[')'] = '(';
		peer['['] = ']', peer[']'] = '[';
		peer['{'] = '}', peer['}'] = '{';
		updown['('] = updown['['] = updown['{'] = 1;
		updown[')'] = updown[']'] = updown['}'] = -1;
	}

	if (ch < 0 || !(dir = updown[ch])) {
		unsigned back, ahead;
		for (back = 1; back <= offset; back++) {
			ch = view_byte(view, offset-back);
			if (updown[ch] < 0)
				if (stackptr == sizeof stack)
					break;
				else
					stack[stackptr++] = ch;
			else if (updown[ch] > 0 &&
				 (!stackptr || ch != peer[stack[--stackptr]]))
				break;
		}
		for (ahead = 1; ; ahead++) {
			ch = view_byte(view, offset+ahead);
			if (ch < 0)
				break;
			if (updown[ch] > 0)
				if (stackptr == sizeof stack)
					break;
				else
					stack[stackptr++] = ch;
			else if (updown[ch] < 0 &&
				 (!stackptr || ch != peer[stack[--stackptr]]))
				break;
		}
		if (back <= ahead && back <= offset)
			return offset - back;
		else if (offset + ahead - 1 < view->bytes)
			return offset + ahead - 1;
		else
			return -1;
	}

	stack[stackptr++] = ch;
	while (stackptr) {
		offset += dir;
		ch = view_byte(view, offset);
		if (ch < 0)
			return -1;
		if (updown[ch] == dir) {
			if (stackptr == sizeof stack)
				return -1;
			stack[stackptr++] = ch;
		} else if (updown[ch] == -dir)
			if (ch != peer[stack[--stackptr]])
				break;
	}

	return offset;
}

char *tab_complete(const char *path)
{
	unsigned len = strlen(path);
	char *new = allocate(NULL, len + NAME_MAX);
	char *p;
	DIR *dir;
	struct dirent *dent, *best_dent = NULL;

	memcpy(new, path, len+1);
	p = strrchr(new, '/');
	if (p) {
		*p = '\0';
		dir = opendir(new);
		*p++ = '/';
	} else {
		dir = opendir(".");
		p = new;
	}
	if (dir) {
		while ((dent = readdir(dir)))
			if (!strncmp(p, dent->d_name, new + len - p) &&
			    (!best_dent ||
			     strcmp(dent->d_name, best_dent->d_name) < 0))
				best_dent = dent;
		if (best_dent)
			strcpy(p, best_dent->d_name);
		closedir(dir);
	}

	if (!strcmp(new, path)) {
		allocate(new, 0);
		new = NULL;
	}
	return new;
}
