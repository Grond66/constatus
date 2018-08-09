
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "constatus.h"

#define CLOCK_SIZE			8

struct clock_ctx {
	char cur_display[CLOCK_SIZE+1];
	char next_display[CLOCK_SIZE+1];
};

void format_time(struct clock_ctx *ctx, struct timespec *time,
		 char *dst, size_t n) {
	time_t now_second;
	struct tm local_time;

	memcpy(dst, "--:--:--", n);

	now_second = time->tv_sec;
	localtime_r(&now_second, &local_time);
	strftime(dst, n, "%T", &local_time);
}

static void *init(void) {
	struct clock_ctx *ret;
	struct timespec now;

	if (!(ret = malloc(sizeof(*ret))) ||
	    clock_gettime(CLOCK_REALTIME, &now))
		return NULL;

	// we need to use localtime_r() later
	tzset();

	format_time(ret, &now, ret->next_display, sizeof(ret->next_display));
	// display() might be called before callback(), so hedge our bets
	memcpy(ret->cur_display, ret->next_display, sizeof(ret->cur_display));

	return ret;
}

static void display(void *instance, WINDOW *win) {
	struct clock_ctx *ctx = instance;

	mvwaddnstr(win, 0, 0, ctx->cur_display, CLOCK_SIZE);
}

static struct timespec callback(void *instance, WINDOW *win) {
	struct clock_ctx *ctx = instance;
	struct timespec now;
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 0, };

	memcpy(ctx->cur_display, ctx->next_display, sizeof(ctx->cur_display));
	display(instance, win);

	// the error handling here is pretty abysmal. clearly we need an error
	// reporting API in the core [TODO]
	if (clock_gettime(CLOCK_REALTIME, &now)) {
		delay.tv_sec = 1;
		return delay;
	}
	++now.tv_sec;

	format_time(ctx, &now, ctx->next_display, sizeof(ctx->next_display));

	if (now.tv_nsec == 0)
		delay.tv_sec = 1;
	else
		delay.tv_nsec = 1000000000 - now.tv_nsec;

	return delay;
}

CONSTATUS_MODULE = {
	.height = 1,
	.width = CLOCK_SIZE,
	.init = &init,
	.callback = &callback,
	.display = &display,
};
