
#include <string.h>
#include <stdlib.h>

#define CONSTATUS_INTERNAL
#include "constatus.h"

#define ASSERT_GADGET_CONTEXT(gadget, retval)				\
	if (!((gadget) = get_gadget_context())) {			\
		constatus_err("attempt to call %s from non-gadget context", \
			      __func__);				\
		return retval;						\
	}

int cmod_resize(int h, int w) {
	struct gadget *g;

	ASSERT_GADGET_CONTEXT(g, -1);

	if (h < 0 || h > screen_height - 1) {
		cmod_err("tried to attain invalid height %i", h);
		return -1;
	}
	if (w < 0 || w > screen_width) {
		cmod_err("tried to attain invalid width %i", w);
		return -1;
	}

	g->height = h;
	g->width = w;

	place_gadgets();

	need_redraw = 1;

	return 0;
}

static void do_message(struct gadget *g, const char *fmt, va_list args,
		       enum message_type type) {
	char *newfmt;
	size_t newfmt_len;

	newfmt_len = strlen(g->name) + sizeof(": ") + strlen(fmt);
	if (!(newfmt = malloc(newfmt_len))) {
		constatus_err("unable to allocate memory for format string");
		return;
	}

	// [TODO] [XXX] if g->name has % substitutions in it, constatus_vmsg()
	// will likely produce incorrect output or crash.
	snprintf(newfmt, newfmt_len, "%s: %s", g->name, fmt);

	constatus_vmsg(newfmt, args, type);

	free(newfmt);
}

extern void cmod_err(const char *fmt, ...) {
	va_list args;
	struct gadget *g;

	ASSERT_GADGET_CONTEXT(g,);

	va_start(args, fmt);

	do_message(g, fmt, args, MSGTYPE_ERROR);

	va_end(args);
}

void cmod_info(const char *fmt, ...) {
	va_list args;
	struct gadget *g;

	ASSERT_GADGET_CONTEXT(g,);

	va_start(args, fmt);

	do_message(g, fmt, args, MSGTYPE_INFO);

	va_end(args);
}
