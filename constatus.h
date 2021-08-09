
#ifndef _CONSTATUS_H_
#define _CONSTATUS_H_

#include <curses.h>
#include <panel.h>
#include <time.h>
#include <stdarg.h>
#include <limits.h>

// all the stuff that's specific to the core program...
#ifdef CONSTATUS_INTERNAL

#define container_of(p, type, member)\
	((type *)((uint8_t *)(p)-(size_t)&(((type *)0)->member)))

struct list {
	struct list *prev, *next;
};

inline static void list_init(struct list *l) {
	l->prev = l;
	l->next = l;
}

inline static void list_append(struct list *l, struct list *el) {
	el->prev = l->prev;
	el->next = l;
	l->prev = el;
	el->prev->next = el;
}

inline static void list_del(struct list *el) {
	el->next->prev = el->prev;
	el->prev->next = el->next;
	list_init(el);
}

inline static int list_is_empty(struct list *l) {
	return l->next == l && l->prev == l;
}

#define list_next(element, type, member)		\
	container_of((element)->member.next, type, member)
#define list_prev(element, type, member)		\
	container_of((element)->member.prev, type, member)
#define list_first(list, type, member)			\
	container_of((list)->next, type, member)
#define list_last(list, type, member)			\
	container_of((list)->prev, type, member)
#define list_is_first(list, element, type, member)	\
	((element)->member.prev == (list))
#define list_is_last(list, element, type, member)	\
	((element)->member.next == (list))


#define LIST_FOR_EACH(list, cur, type, member)				\
	for ((cur) = container_of((list)->next, type, member);		\
	     &(cur)->member != (list);					\
	     (cur) = container_of((cur)->member.next, type, member))
#define LIST_FOR_EACH_DELETE(list, cur, save, type, member)		\
	for ((cur) = container_of((list)->next, type, member),		\
	     (save) = container_of((cur)->member.next, type, member);	\
	     &(cur)->member != (list);					\
	     (cur) = (save),						\
	     (save) = container_of((cur)->member.next, type, member))

struct gadget {
	struct list list;
	int height, width;
	int x, y;
	struct constatus_module *module;
	void *instance;
	WINDOW *window;
	PANEL *panel;
};

struct page {
	struct list list;
	struct list gadgets;
};

#endif /* CONSTATUS_INTERNAL */

// the interfaces exposed to client modules...

static inline struct timespec timespec_subtract(struct timespec *a,
					        struct timespec *b) {
	struct timespec ret;

	ret.tv_sec = a->tv_sec - b->tv_sec;
	ret.tv_nsec = a->tv_nsec - b->tv_nsec;
	if (ret.tv_nsec < 0) {
		--ret.tv_sec;
		ret.tv_nsec += 1000000000;
	}

	return ret;
}

static inline struct timespec timespec_add(struct timespec *a,
					   struct timespec *b) {
	struct timespec ret;

	ret.tv_sec = a->tv_sec + b->tv_sec;
	ret.tv_nsec = a->tv_nsec + b->tv_nsec;
	if (ret.tv_nsec >= 1000000000) {
		++ret.tv_sec;
		ret.tv_nsec -= 1000000000;
	}

	return ret;
}

typedef void *(*constatus_init_func)(void);
typedef struct timespec (*constatus_cb_func)(void *, WINDOW *);
typedef void (*constatus_disp_func)(void *, WINDOW *);
struct constatus_module {
	int height, width;
	constatus_init_func init;
	constatus_cb_func callback;
	constatus_disp_func display;
};
#define CONSTATUS_MODULE		struct constatus_module module_table


#endif /* _CONSTATUS_H_ */
