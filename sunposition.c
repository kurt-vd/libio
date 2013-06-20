#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <math.h>

#include "sun.h"

#define KEERKRING  23.45

int where_is_the_sun(time_t now, double north, double east,
		double *pincl, double *pazimuth)
{
	double pyear, pday;
	double incl, azimuth;
	double real_eq;

	time_t t0, te;
	int daysecs;
	struct tm tmnow, tmref = {
		/* 21 march, 13h */
		.tm_mon = 2,
		.tm_mday = 21,
		.tm_hour = 13,
	};
	
	/* verify parameters */
	if (fabs(north) > 90)
		return -1;
	if (fabs(east) > 180)
		return -1;
	if (!gmtime_r(&now, &tmnow))
		return -1;

	/* find out the last & next 21 march */
	tmref.tm_year = tmnow.tm_year;
	t0 = timegm(&tmref);
	if (t0 >= now) {
		te = t0;
		tmref.tm_year -= 1;
		t0 = timegm(&tmref);
	} else {
		tmref.tm_year += 1;
		te = timegm(&tmref);
	}

	pyear = (now - t0) * 1.0 / (te - t0);
	daysecs = (tmnow.tm_hour * 60 + tmnow.tm_min) * 60 + tmnow.tm_sec;

	/*
	 * The sun describes a circle, 0° at 6h, 90° at 12h, ...
	 * with 0° equals east, 90=south(?), 180 west, 270 north
	 * 
	 * sun position (angle with 1=360°) within day, at Greenwich
	 * - 0° is at 6h, East
	 * - 90° is at 12h, South/North
	 * - 180° is at 18h, West
	 * - 270° is at 0h, North/South
	 */
	pday = (daysecs - 6*3600) / 86400.0;

	/* adjust eastern position */
	pday += east / 360.0;
	
	/* calculate sun's inclination on equator*/
	incl = sin(2.0 * M_PI * pday) * 90;

	/*
	 * adjust for northern offset:
	 * during one year, the 'real' equator (where sun raises up to 90°
	 * rotates between KEERKRING (juin) & -KEERKRING (december)
	 */
	real_eq = sin(2.0 * M_PI * pyear) * KEERKRING;
	incl = incl * ((90.0 - fabs(north))/90.0) + real_eq;
	
	/* get "azimuth" (0=N/S, 90=E, 180=S/N, 270=W) */
	azimuth = 360 + 90 - (pday * 360);
	if (north < 0)
		azimuth = 180 - azimuth;

	/* adjust */
	if (incl >= 90.0) {
		incl = 180.0 - incl;
	} else if (incl <= -90.0) {
		incl = -180.0 - incl;
	} else {
		/* flip the day_pos */
		azimuth = 180 - azimuth;
	}
	/* normalize */
	while (azimuth > +180)
		azimuth -= 360;
	while (azimuth < -180)
		azimuth += 360;

	*pincl = incl;
	*pazimuth = azimuth;

	return 0;
}
