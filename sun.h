#include <time.h>

#ifndef _sun_h_
#define _sun_h_

#ifdef __cplusplus
extern "C" {
#endif

extern int where_is_the_sun(time_t now, double north, double east,
		double *pincl, double *pazimuth);

extern const double default_gpslat, default_gpslon;

#ifdef __cplusplus
}
#endif
#endif
