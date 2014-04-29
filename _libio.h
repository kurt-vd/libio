#ifndef __libio_h_
#define __libio_h_

#include "libio.h"

/* IOPAR definitions */
struct iopar {
	int id;
	int state;
#define ST_DIRTY	0x01
#define ST_PRESENT	0x08 /* parameter has real data */
	double value;
	void (*del)(struct iopar *);
	int (*set)(struct iopar *, double value);
	/* method to refresh value just before get */
	void (*jitget)(struct iopar *);
	/* direct event notification */
	struct iopar_notifier {
		struct iopar_notifier *next;
		void *dat;
		void (*fn)(void *dat);
	} *notifiers;
};

static inline void iopar_set_dirty(struct iopar *iopar)
{
	struct iopar_notifier *notifier;

	iopar->state |= ST_DIRTY;
	for (notifier = iopar->notifiers; notifier; notifier = notifier->next)
		notifier->fn(notifier->dat);
}

static inline void iopar_set_present(struct iopar *iopar)
{
	if (!(iopar->state & ST_PRESENT))
		iopar->state |= ST_PRESENT | ST_DIRTY;
}

static inline void iopar_clr_present(struct iopar *iopar)
{
	if (iopar->state & ST_PRESENT)
		iopar->state = (iopar->state | ST_DIRTY) & ~ST_PRESENT;
}

extern int libio_trace;

extern void netio_sync(void);
extern void longdet_flush(void);

/* raw create function */
extern struct iopar *create_libiopar(const char *str);

/* in-library access to parameter table for #id to struct lookup */
extern struct iopar *lookup_iopar(int iopar_id);

/* cleanup iopar, just before freeing */
extern void cleanup_libiopar(struct iopar *iopar);

/* direct event notifications */
extern int iopar_add_notifier(int iopar, void (*)(void *), void *dat);
extern int iopar_del_notifier(int iopar, void (*)(void *), void *dat);

/* real parameter constructors */
extern struct iopar *mkpreset(char *str);
extern struct iopar *mkvirtual(char *str);
extern struct iopar *mkshared(char *cstr);
extern struct iopar *mkled(char *str);
extern struct iopar *mkbacklight(char *str);
extern struct iopar *mkinputevbtn(char *str);
extern struct iopar *mkapplelight(char *sysfs);
extern struct iopar *mkcpupar(char *sysfs);
extern struct iopar *mkmotordir(char *str);
extern struct iopar *mkmotorpos(char *str);
extern struct iopar *mkteleruptor(char *str);
extern struct iopar *mkvirtualteleruptor(char *str);

extern struct iopar *mknetiolocal(char *name);
extern struct iopar *mknetiounix(char *uri);
extern struct iopar *mknetioudp4(char *uri);
extern struct iopar *mknetioudp6(char *uri);

#endif
