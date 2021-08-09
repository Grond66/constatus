
#include "constatus.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <libbatt.h>

#define min(a, b)			(((a) < (b)) ? (a) : (b))
#define max(a, b)			(((a) > (b)) ? (a) : (b))

struct linux_battery_ctx {
	int resize_error;
	struct batt_probe *batts;
	int n_batts;
	int cur_height, cur_width;
	char *row;
};

static void *init() {
	int i;
	struct linux_battery_ctx *ctx;

	if (!(ctx = malloc(sizeof(*ctx))))
		return NULL;

	ctx->n_batts = 0;
	if ((ctx->n_batts = batt_open_all(&ctx->batts)) < 0) {
		cmod_err("error opening system batteries");
		goto err;
	}

	for (i = 0; i < ctx->n_batts; ++i)
		batt_read_data(ctx->batts + i);

	ctx->cur_height = 0;
	ctx->cur_width = 0;
	ctx->row = NULL;

	return ctx;

   err:
	free(ctx);

	return NULL;
}

static void draw_error_bar(struct linux_battery_ctx *ctx, WINDOW *win,
			   int index) {
	int len, j, limit;

	len = snprintf(ctx->row, ctx->cur_width, "ERROR: %s",
		       ctx->batts[index].name);
	if (len < ctx->cur_width) {
		memset(ctx->row + len, ' ', ctx->cur_width - len);
		ctx->row[ctx->cur_width] = 0;
	}

	wmove(win, index, 0);
	limit = min(len, ctx->cur_width);
	for (j = 0; j < limit; ++j)
		waddch(win, ctx->row[j] | A_REVERSE);
	for (; j < ctx->cur_width; ++j)
		waddch(win, ctx->row[j]);
}

static void draw_resize_error(struct linux_battery_ctx *ctx, WINDOW *win) {
	int mid_row, start_col, i, limit;

	werase(win);

	mid_row = (int) floor(ctx->cur_height / 2.0);
	start_col = lround((double)(ctx->cur_width - sizeof("ERROR RESIZE")-1)
			   / 2.0);
	start_col = max(start_col, 0);

	wmove(win, mid_row, start_col);
	limit = min(sizeof("ERROR RESIZE")-1, ctx->cur_width);
	for (i = 0; i < limit; ++i)
		waddch(win, "ERROR RESIZE"[i] | A_REVERSE);
	for (; i < ctx->cur_width; ++i)
		waddch(win, ' ' | A_REVERSE);
}

static void display(void *instance, WINDOW *win) {
	struct linux_battery_ctx *ctx = (struct linux_battery_ctx *) instance;
	int i, j, msg_len, msg_start, percentile_len;
	double percent;

	if (ctx->resize_error) {
		draw_resize_error(ctx, win);
		return;
	}

	for (i = 0; i < ctx->n_batts; ++i) {
		if (batt_get_percentage(ctx->batts + i, &percent)) {
			draw_error_bar(ctx, win, i);
			continue;
		}

		// 3 spaces for a floating point number formatted with % 3.0f
		// and one space for a percent sign
		msg_len = strlen(ctx->batts[i].name) + sizeof(": ")-1 + 3 + 1;
		msg_start = lround((double)(ctx->cur_width - msg_len) / 2.0);

		if (msg_start < 0) {
			cmod_err("%s data to wide to fit on screen",
				 ctx->batts[i].name);
			draw_error_bar(ctx, win, i);
			continue;
		}

		snprintf(ctx->row + msg_start, msg_len + 1, "%s: %3.0f%%",
			 ctx->batts[i].name, percent * 100);
		memset(ctx->row, ' ', msg_start);
		memset(ctx->row + msg_start + msg_len, ' ',
		       ctx->cur_width - msg_start - msg_len);
		ctx->row[ctx->cur_width] = '\0';

		percentile_len = lround((double)ctx->cur_width * percent);

		wmove(win, i, 0);
		for (j = 0; j < percentile_len; ++j)
			waddch(win, ctx->row[j] | A_REVERSE);
		for (; j < ctx->cur_width; ++j)
			waddch(win, ctx->row[j]);
	}
}

static void resize(void *instance, int screen_height, int screen_width) {
	struct linux_battery_ctx *ctx = (struct linux_battery_ctx *) instance;
	void *tmp;

	if (cmod_resize(ctx->n_batts, screen_width)) {
		cmod_err("error resizing to fit console");
		goto err;
	}

	if (!(tmp = realloc(ctx->row, screen_width+1))) {
		cmod_err("cannot allocate formatting buffer");
		goto err;
	}
	ctx->row = tmp;

	ctx->cur_height = ctx->n_batts;
	ctx->cur_width = screen_width;
	ctx->resize_error = 0;

	return;

   err:
	ctx->resize_error = 1;
}

static struct timespec callback(void *instance, WINDOW *win) {
	struct linux_battery_ctx *ctx = (struct linux_battery_ctx *) instance;
	struct timespec delay = {
		.tv_sec = 1,
		.tv_nsec = 0,
	};
	int i;

	for (i = 0; i < ctx->n_batts; ++i)
		batt_read_data(ctx->batts + i);

	display(instance, win);

	return delay;
}

CONSTATUS_MODULE = {
	.height = 1,
	.width = 1,
	.init = &init,
	.display = &display,
	.callback = &callback,
	.resize = &resize,
};
