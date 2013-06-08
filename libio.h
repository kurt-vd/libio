#ifndef _libio_h_
#define _libio_h_

#ifdef __cplusplus
extern "C" {
#endif

/* GENERIC */

/* malloc & zero */
extern void *zalloc(unsigned int size);

/* sysfs (or any other file) iface */
extern int attr_read(int default_value, const char *fmt, ...)
	__attribute__((format(printf,2,3)));
extern int attr_write(int value, const char *fmt, ...)
	__attribute__((format(printf,2,3)));

/* set single event with itimer */
extern int schedule_itimer(double value);

struct iopar;

/* parameter API */
extern int create_iopar(const char *str);
extern int create_iopar_type(const char *type, const char *spec);
extern void destroy_iopar(int iopar);
extern double get_iopar(int iopar, double default_value);
extern int set_iopar(int iopar, double value);

/* return true when iopar is dirty */
extern int iopar_dirty(int iopar);
/* return true when iopar is lost */
extern int iopar_present(int iopar);

/*
 * Only signal (aka. dirty) 'on' state
 * This will affect only some types of iopar's!
 */
extern int iopar_set_pushbtn(int iopar);

/* netio: publish local parameter via this socket */
extern int libio_bind_net(const char *uri);

/*
 * iopars_flush
 * called at each main cycle, clears dirty flags
 */
extern void libio_flush(void);

/* set verbosity of libio */
extern void libio_set_trace(int value);

#ifdef __cplusplus
}
#endif
#endif
