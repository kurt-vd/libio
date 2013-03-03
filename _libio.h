#ifndef __libio_h_
#define __libio_h_

#include "libio.h"

/* IOPAR definitions */
struct iopar {
	int id;
	int state;
#define ST_DIRTY	0x01
#define ST_WAITING	0x02 /* waiting for transmission, ... */
#define ST_WRITABLE	0x04 /* remotes may write */
#define ST_PRESENT	0x08 /* parameter has real data */
#define ST_PUSHBTN	0x10 /* for input device: only signal 'on' state */
	double value;
	void (*del)(struct iopar *);
	int (*set)(struct iopar *, double value);
};

static inline void iopar_set_dirty(struct iopar *iopar)
{
	iopar->state |= ST_DIRTY;
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

extern struct iopar *mkled(const char *str);
extern struct iopar *mkledbool(const char *str);
extern struct iopar *mkbacklight(const char *str);
extern struct iopar *mkinputevbtn(const char *str);
extern struct iopar *mkapplelight(const char *sysfs);

extern struct iopar *mknetiolocal(const char *uri);
extern struct iopar *mknetiounix(const char *uri);
extern struct iopar *mknetioudp4(const char *uri);
extern struct iopar *mknetioudp6(const char *uri);

#endif
