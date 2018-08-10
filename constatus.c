
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <panel.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <dlfcn.h>
#include <limits.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <ctype.h>

#include <libconfig.h>

#ifndef _POSIX_MONOTONIC_CLOCK
# error "constatus needs CLOCK_MONOTONIC to work properly"
#endif

#include "constatus.h"

#define const_strlen(str)		(sizeof(str)-1)
#define max(a, b)			(((a) > (b)) ? (a) : (b))
#define min(a, b)			(((a) < (b)) ? (a) : (b))
#define array_size(arr)			(sizeof(arr)/sizeof(*arr))

#define BANNER_TEXT			"constatus q:quit"
#define SYSTEM_MODULE_DIR		"/usr/lib/constatus/modules"
#define SYSTEM_CONF_DIR			"/etc"
#define CONF_NAME			"constatus.rc"

struct wakeup {
	struct timespec time;
	struct gadget *gadget;
};

enum {
	COLOR_PAIR_BANNER = 1,
	COLOR_PAIR_ERR,
	NEEDED_COLOR_PAIRS,
};

static int screen_height, screen_width;
static struct gadget *gadgets = NULL;
static size_t n_gadgets = 0;
static struct page *cur_page = NULL;
static struct list pages;
static struct wakeup *wakeups = NULL;
static size_t n_wakeups = 0;
static size_t wakeups_size = 0;
static int curses_active = 0;
static char *home_dir = NULL;
static char *conf_file = NULL;
static char *module_dir = NULL;

static int cleanup(void) {
	if (curses_active && endwin() == ERR) {
		warnx("error leaving curses mode; screen may be corrupt");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static void panic(const char *fmt, ...) {
	va_list args;

	cleanup();

	va_start(args, fmt);

	verr(EXIT_FAILURE, fmt, args);
}

static void panicx(const char *fmt, ...) {
	va_list args;

	cleanup();

	va_start(args, fmt);

	verrx(EXIT_FAILURE, fmt, args);
}

static void setup_color_palate(void) {
	if (!has_colors() || NEEDED_COLOR_PAIRS >= COLOR_PAIRS)
		return;

	if (init_pair(COLOR_PAIR_BANNER, COLOR_BLACK, COLOR_RED) == ERR ||
	    init_pair(COLOR_PAIR_ERR, COLOR_YELLOW, COLOR_RED) == ERR)
		panicx("error initializing color pairs");
}

static void draw_banner() {
	int i;

	attron(COLOR_PAIR(COLOR_PAIR_BANNER));
	mvaddnstr(0, 0, BANNER_TEXT, screen_width);
	for (i = const_strlen(BANNER_TEXT); i < screen_width; ++i)
		addch(' ');
	attroff(COLOR_PAIR(COLOR_PAIR_BANNER));
}

static void set_error_banner(const char *fmt, ...) {
	// done this way so that memory allocation failure is impossible
	static char text[512];
	va_list args;

	draw_banner();

	va_start(args, fmt);

	vsnprintf(text, min(sizeof(text), screen_width+1), fmt, args);

	attron(COLOR_PAIR(COLOR_PAIR_ERR) | A_BOLD);
	mvaddstr(0, 0, text);
	attroff(COLOR_PAIR(COLOR_PAIR_ERR) | A_BOLD);

	va_end(args);
}

static void center_gadget_row(int first, int last, int row_height, int row_width) {
	int i;
	int horz_shift = floor((double)(screen_width - row_width)/2.0);

	for (i = first; i <= last; ++i) {
		gadgets[i].y +=
			ceil((double)(row_height - gadgets[i].height)/2.0);
		gadgets[i].x += horz_shift;
	}
}

static void center_gadget_page(int first, int last, int page_height) {
	int i;
	int vert_shift = floor((double)(screen_height - page_height)/2.0);

	for (i = first; i <= last; ++i)
		gadgets[i].y += vert_shift;
}

static struct page *add_page(void) {
	struct page *ret;

	if (!(ret = malloc(sizeof(*ret))))
		return NULL;

	list_init(&ret->gadgets);
	list_append(&pages, &ret->list);

	return ret;
}

static void clear_pages(void) {
	struct page *next_page;
	struct gadget *cur_gadget, *next_gadget;

	LIST_FOR_EACH_DELETE(&pages, cur_page, next_page, struct page, list) {
		LIST_FOR_EACH_DELETE(&cur_page->gadgets, cur_gadget, next_gadget,
				     struct gadget, list) {
			del_panel(cur_gadget->panel);
			delwin(cur_gadget->window);
			list_del(&cur_gadget->list);
		}
		list_del(&cur_page->list);
		free(cur_page);
	}
	cur_page = NULL;
}

static int show_page(struct page *pg) {
	struct gadget *g;

	LIST_FOR_EACH(&pg->gadgets, g, struct gadget, list)
		if (show_panel(g->panel) == ERR) {
			set_error_banner("error showing active gadget set");
			return -1;
		}

	return 0;
}

static int hide_page(struct page *pg) {
	struct gadget *g;

	LIST_FOR_EACH(&pg->gadgets, g, struct gadget, list)
		if (hide_panel(g->panel) == ERR) {
			set_error_banner("error hiding active gadget set");
			return -1;
		}

	return 0;
}

static int add_gadget(struct constatus_module *module) {
	void *tmp;

	if (!module->init || !module->display || !module->callback) {
		errno = EINVAL;
		return -1;
	}

	if (!(tmp = realloc(gadgets, (n_gadgets+1) * sizeof(*gadgets))))
		return -1;
	gadgets = tmp;

	memset(gadgets + n_gadgets, '\0', sizeof(*gadgets));
	gadgets[n_gadgets].module = module;
	gadgets[n_gadgets].height = module->height;
	gadgets[n_gadgets].width = module->width;

	set_gadget_context(gadgets + n_gadgets);
	gadgets[n_gadgets].instance = module->init();
	clear_gadget_context();

	if (!gadgets[n_gadgets].instance)
		return -1;

	++n_gadgets;

	return 0;
}

#define NUM_TEST_GADGETS		11
static void place_gadgets(void) {
	int i;
	int next_y = 1, next_x = 0;
	int biggest_height = 0;
	int row_start = 0;
	int page_start = 0;

	clear_pages();

	for (i = 0; i < n_gadgets; ++i) {
	  retry:
		if (gadgets[i].width > screen_width ||
		    gadgets[i].height > screen_height-1) {
			set_error_banner("unable to place gadget: too large for screen");
			goto err;
		}

		if (list_is_empty(&pages) ||
		    next_y + gadgets[i].height > screen_height)
			goto new_page;
		if (next_x + gadgets[i].width > screen_width) {
			next_y += biggest_height;
			goto new_row;
		}

		gadgets[i].x = next_x;
		gadgets[i].y = next_y;
		list_append(&cur_page->gadgets, &(gadgets[i].list));

		next_x += gadgets[i].width + 1;
		biggest_height = max(biggest_height, gadgets[i].height);

		continue;

	  new_page:
		if (!(cur_page = add_page()))
			goto err;

		center_gadget_page(page_start, i-1, next_y + biggest_height);

		next_y = 1;
		page_start = i;

	  new_row:
		center_gadget_row(row_start, i-1, biggest_height, next_x - 1);

		biggest_height = 0;
		next_x = 0;
		row_start = i;

		goto retry;
	}
	next_y += biggest_height;
	center_gadget_row(row_start, i-1, biggest_height, next_x - 1);
	center_gadget_page(page_start, i-1, next_y);

	for (i = 0; i < n_gadgets; ++i) {
		if (!(gadgets[i].window = newwin(gadgets[i].height,
						 gadgets[i].width,
						 gadgets[i].y,
						 gadgets[i].x)) ||
		    !(gadgets[i].panel = new_panel(gadgets[i].window)) ||
		    hide_panel(gadgets[i].panel) == ERR) {
			set_error_banner("cannot allocate windows for gadgets");
			goto err;
		}
	}

	cur_page = list_first(&pages, struct page, list);
	if (show_page(cur_page))
		goto err;

	return;

  err:
	clear_pages();
}

static void draw_current_page(void) {
	struct gadget *g = NULL;

	if (!cur_page)
		return;

	LIST_FOR_EACH(&cur_page->gadgets, g, struct gadget, list)
		g->module->display(g->instance, g->window);
}

static void update_layout_and_draw(void) {
	clear();
	draw_banner(screen_width);

	place_gadgets();
	draw_current_page();
}

static inline int timespec_lt(struct timespec *a, struct timespec *b) {
	return (a->tv_sec < b->tv_sec) ?
		1
	: (a->tv_sec > b->tv_sec) ?
		0
	:
		(a->tv_nsec < b->tv_nsec);
}

static inline int wakeup_sooner(struct wakeup *a, struct wakeup *b) {
	return timespec_lt(&a->time, &b->time);
}

static inline size_t parent_index(size_t i) {
	return (i - 1) / 2;
}

static inline void wakeup_swap(struct wakeup *a, struct wakeup *b) {
	struct wakeup tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int queue_wakeup(struct wakeup *wake) {
	void *tmp;
	size_t i;
	size_t next_size;

	next_size = max(wakeups_size, n_wakeups + 1);
	if (!(tmp = realloc(wakeups, next_size * sizeof(*wakeups))))
		return -1;
	wakeups = tmp;
	wakeups_size = next_size;

	wakeups[n_wakeups] = *wake;

	for (i = n_wakeups;
	     i > 0 && wakeup_sooner(wakeups + i, wakeups + parent_index(i));
	     i = parent_index(i))
		wakeup_swap(wakeups + i, wakeups + parent_index(i));

	++n_wakeups;

	return 0;
}

static inline struct wakeup *peek_next_wakeup(void) {
	return (n_wakeups == 0) ? NULL : wakeups;
}

static inline size_t nth_child(size_t i, size_t child) {
	return 2 * i + child;
}

// find the index of child wakeup node in the queue with the soonest wakeup time
// including the parent node. returns the index of the soonest-to-be-woken
// child, or i if no child is to be woken sooner than wakups[i].
static inline size_t find_soonest_child_index(size_t i) {
	size_t ret = i;
	struct timespec soonest_time = wakeups[i].time;
	size_t j;

	for (j = 1; j <= 2; ++j) {
		if (nth_child(i, j) >= n_wakeups)
			break;

		if (timespec_lt(&wakeups[nth_child(i, j)].time,
				&soonest_time)) {
			ret = nth_child(i, j);
			soonest_time = wakeups[nth_child(i, j)].time;
		}
	}

	return ret;
}

static int pop_next_wakeup(struct wakeup *res) {
	size_t i, next_i;

	if (n_wakeups == 0)
		return -1;

	*res = wakeups[0];

	wakeups[0] = wakeups[--n_wakeups];
	for (i = 0; i < n_wakeups; i = next_i) {
		next_i = find_soonest_child_index(i);

		// if we can no longer make forward progress...
		if (next_i <= i)
			break;

		wakeup_swap(wakeups + i, wakeups + next_i);
	}

	return 0;
}

// check if using the expression a*b will overflow
static inline int mult_overflow(long a, long b) {
	return
		(a == -1 && b == LONG_MIN) ||
		(b == -1 && a == LONG_MIN) ||
		a > LONG_MAX / b ||
		// this works because integer division always truncates towards
		// zero, not negative infinity
		a < LONG_MIN / b;
}

// check if using the expression a+b will overflow
// if it seems like this is missing some cases, remember the mixed sign pairs
// cannot overflow...
static inline int add_overflow(long a, long b) {
	return
		(a > 0 && b > LONG_MAX - a) ||
		(a < 0 && b < LONG_MIN - a);
}

// convert a timespec into a number of miliseconds represented as an int.
// in the case of overflow, return -1 and set the result (stored in res)
// to INT_MAX. truncates any nanosecond precision.
static int timespec_to_milis(struct timespec *ts, int *res) {
	long tmp;

	if (mult_overflow(ts->tv_sec, 1000))
		goto overflow;
	tmp = ts->tv_sec * 1000;

	if (add_overflow(tmp, ts->tv_nsec / 1000000))
		goto overflow;
	tmp += ts->tv_nsec / 1000000;

	if (tmp != (int)tmp)
		goto overflow;

	*res = tmp;

	return 0;

  overflow:
	*res = INT_MAX;

	return -1;
}

static void callback_gadget(struct gadget *g) {
	struct timespec now, delay;
	struct wakeup w;
	delay = g->module->callback(g->instance, g->window);

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		panic("error getting current time");

	w.gadget = g;
	w.time = timespec_add(&now, &delay);

	if (queue_wakeup(&w))
		panic("error queuing wakeup");
}

static int handle_keypress(void) {
	chtype c;
	struct page *old_page;

	c = getch();

	// timeout, no key was pressed; probably a false alarm generated by the
	// receipt of a signal
	if (c == ERR)
		return 0;

	// a request to quit...
	if (c == 'q')
		return 1;

	switch (c) {
	case KEY_RESIZE:
		getmaxyx(stdscr, screen_height, screen_width);
		update_layout_and_draw();
	break;
	case KEY_LEFT:
		if (!cur_page ||
		    list_is_first(&pages, cur_page, struct page, list))
			break;

		old_page = cur_page;
		if (hide_page(old_page))
			break;
		cur_page = list_prev(cur_page, struct page, list);
		if (show_page(cur_page)) {
			show_page(old_page);
			cur_page = old_page;
			break;
		}

		clear();
		draw_banner();
		draw_current_page();
	break;
	case KEY_RIGHT:
		if (!cur_page ||
		    list_is_last(&pages, cur_page, struct page, list))
			break;

		old_page = cur_page;
		if (hide_page(old_page))
			break;
		cur_page = list_next(cur_page, struct page, list);
		if (show_page(cur_page)) {
			show_page(old_page);
			cur_page = old_page;
			break;
		}

		clear();
		draw_banner();
		draw_current_page();
	break;
	default:
		return 0;
	}

	update_panels();
	doupdate();

	return 0;
}

static int path_search(const char **dirs, int n_dirs, const char *fname,
		       char *buf, size_t buf_size) {
	int i;
	struct stat stats;

	for (i = 0; i < n_dirs; ++i) {
		if (!dirs[i])
			continue;

		snprintf(buf, buf_size, "%s/%s", dirs[i], fname);

		if (stat(buf, &stats) == 0)
			break;
	}

	if (i >= n_dirs)
		return -1;

	return 0;
}

void load_gadget(const char *name) {
	void *obj;
	struct constatus_module *module;
	const char *module_dirs[2];
	char libname[_POSIX_PATH_MAX+1];
	char libpath[_POSIX_PATH_MAX+1];
	char user_mod_dir_buf[_POSIX_PATH_MAX+1];
	char *user_mod_dir = NULL;
	size_t i;

	if (module_dir) {
		module_dirs[0] = module_dir;
		module_dirs[1] = NULL;
	} else {
		if (home_dir) {
			snprintf(user_mod_dir_buf, sizeof(user_mod_dir_buf),
				 "%s/modules", home_dir);
			user_mod_dir = user_mod_dir_buf;
		}
		module_dirs[0] = user_mod_dir;
		module_dirs[1] = SYSTEM_MODULE_DIR;
	}

	snprintf(libname, sizeof(libname), "%s.so", name);
	if (path_search(module_dirs, array_size(module_dirs), libname, libpath,
			sizeof(libpath))) {
		cleanup();
		warnx("error loading module: cannot find %s in:", libname);
		for (i = 0; i < array_size(module_dirs); ++i)
			if (module_dirs[i])
				warnx("   %s", module_dirs[i]);
		exit(EXIT_FAILURE);
	}

	if (!(obj = dlopen(libpath, RTLD_NOW | RTLD_LOCAL)))
		panicx("error loading module: %s", dlerror());

	if (!(module = dlsym(obj, "module_table")))
		panicx("could not find symbol `module_table' in module file: %s",
		     dlerror());

	if (add_gadget(module))
		panicx("error adding module %s", name);
}

void process_load_section(const char *conf_file, config_setting_t *load) {
	int i;
	config_setting_t *module_name;

	if (config_setting_is_list(load) == CONFIG_FALSE)
		errx(EXIT_FAILURE,
		     "%s:%d: the 'load' config setting must be a list",
		     conf_file, config_setting_source_line(load));

	for (i = 0; i < config_setting_length(load); ++i) {
		module_name = config_setting_get_elem(load, i);
		if (config_setting_type(module_name) != CONFIG_TYPE_STRING)
			errx(EXIT_FAILURE,
			     "%s:%d: module names must be strings",
			     conf_file,
			     config_setting_source_line(module_name));

		load_gadget(config_setting_get_string(module_name));
	}
}

void handle_conf_file_error(const char *conf_file, config_t *cfg) {
	if (config_error_type(cfg) == CONFIG_ERR_FILE_IO)
		errx(EXIT_FAILURE, "%s: %s", conf_file, config_error_text(cfg));
	else if (config_error_type(cfg) == CONFIG_ERR_PARSE)
		errx(EXIT_FAILURE, "%s:%d: %s", conf_file,
		     config_error_line(cfg), config_error_text(cfg));
	else
		errx(EXIT_FAILURE, "unhandled config file error in %s",
		     conf_file);
}

void process_conf_file(const char *conf_file) {
	config_t cfg;
	config_setting_t *load_list;
	FILE *conf_fh;

	if (!(conf_fh = fopen(conf_file, "r")))
		err(EXIT_FAILURE, "error opening config file %s", conf_file);

	config_init(&cfg);
	if (config_read(&cfg, conf_fh) == CONFIG_FALSE)
		handle_conf_file_error(conf_file, &cfg);

	fclose(conf_fh);

	if ((load_list = config_lookup(&cfg, "load")))
		process_load_section(conf_file, load_list);

	config_destroy(&cfg);
}

int main(int argc, char **argv) {
	size_t i;
	struct wakeup wakeup, *wakeup_p;
	struct timespec now, delay;
	struct pollfd stdin_poll;
	int overflow, milis, s;
	char *home;
	char home_dir_buf[_POSIX_PATH_MAX+1];
	char conf_file_buf[_POSIX_PATH_MAX+1];
	char module_dir_buf[_POSIX_PATH_MAX+1];
	struct option longopts[] = {
		{"module-dir",	required_argument,	NULL,	0},
		{"config-file",	required_argument,	NULL,	1},
		{NULL,		0,			NULL,	0},
	};
	int opt;

	list_init(&pages);
	cur_page = NULL;

	opterr = 0;
	optopt = 0;
	while ((opt = getopt_long(argc, argv, "+:m:c:", longopts,
				  NULL)) != -1) {
		switch (opt) {
		case 'm':
		case 0:
			snprintf(module_dir_buf, sizeof(module_dir_buf), "%s",
				optarg);
			module_dir = module_dir_buf;
		break;
		case 'c':
		case 1:
			snprintf(conf_file_buf, sizeof(conf_file_buf), "%s",
				 optarg);
			conf_file = conf_file_buf;
		break;
		case '?':
			if (optopt)
				errx(EXIT_FAILURE, "unknown argument '-%c'",
				     (char) optopt);
			else
				errx(EXIT_FAILURE, "unknown argument '%s'",
					argv[optind-1]);
		break;
		case ':':
			if (isalpha(optopt))
				errx(EXIT_FAILURE, "-%c requires an argument",
				     (char) optopt);
			else
				errx(EXIT_FAILURE, "--%s requires an argument",
				     longopts[optopt].name);
		break;
		}
	}

	if ((home = getenv("HOME"))) {
		snprintf(home_dir_buf, sizeof(home_dir_buf), "%s/.constatus",
			 home);
		home_dir = home_dir_buf;
	}

	if (!conf_file) {
		const char *conf_dirs[] = {
			home_dir,
			SYSTEM_CONF_DIR,
		};
		if (path_search(conf_dirs, array_size(conf_dirs), CONF_NAME,
				conf_file_buf, sizeof(conf_file_buf)) == 0)
			conf_file = conf_file_buf;
	}

	if (conf_file)
		process_conf_file(conf_file);

	if (n_gadgets <= 0)
		panicx("no gadgets loaded; aborting");

	if (!initscr() || start_color() == ERR ||
	    cbreak() == ERR || noecho() == ERR ||
	    keypad(stdscr, TRUE) == ERR || nonl() == ERR ||
	    intrflush(stdscr, FALSE) == ERR || curs_set(0) == ERR ||
	    nodelay(stdscr, TRUE) == ERR)
		panicx("error initializing curses");
	curses_active = 1;
	setup_color_palate();
	getmaxyx(stdscr, screen_height, screen_width);

	update_layout_and_draw();
	for (i = 0; i < n_gadgets; ++i)
		callback_gadget(gadgets + i);
	update_panels();
	doupdate();

	stdin_poll.fd = STDIN_FILENO;
	stdin_poll.events = POLLIN;
	while (1) {
		wakeup_p = peek_next_wakeup();

		if (clock_gettime(CLOCK_MONOTONIC, &now))
			panic("error getting current time");

		delay = timespec_subtract(&wakeup_p->time, &now);
		overflow = timespec_to_milis(&delay, &milis);
		// in case we've overshot...
		if (milis < 0)
			milis = 0;

		s = poll(&stdin_poll, 1, milis);
		if (s < 0 && errno != EINTR)
			panic("error polling the input sources");

		if (((s > 0 && stdin_poll.revents & POLLIN) || s < 0) &&
		    handle_keypress())
			break;

		if (s == 0 && !overflow) {
			pop_next_wakeup(&wakeup);
			callback_gadget(wakeup.gadget);
			update_panels();
			doupdate();
		}
	}

	clear_pages();
	free(gadgets);

	return cleanup();
}
