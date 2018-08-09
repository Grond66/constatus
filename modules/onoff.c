
#include <stdlib.h>

#include "constatus.h"

struct onoff_ctx {
	int is_on;
};

static void *init(void) {
	struct onoff_ctx *ret;

	if (!(ret = malloc(sizeof(*ret))))
		return NULL;

	ret->is_on = 1;

	return ret;
}

static void display(void *instance, WINDOW *win) {
	struct onoff_ctx *ctx = instance;

	mvwaddch(win, 0, 0, (ctx->is_on) ? '*' : ' ');
}

static struct timespec callback(void *instance, WINDOW *win) {
	struct onoff_ctx *ctx = instance;
	struct timespec delay = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};

	display(instance, win);

	ctx->is_on = (ctx->is_on + 1) % 2;

	return delay;
}

CONSTATUS_MODULE = {
	.height = 1,
	.width = 1,
	.init = &init,
	.callback = &callback,
	.display = &display,
};
