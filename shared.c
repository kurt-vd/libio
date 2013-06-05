#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include <error.h>

#include "_libio.h"

/* shared wrapper over other params */
struct shared {
	int refpar; /* referenced parameter, created here */
	int refcnt; /* usage counter */
	int busycnt; /* referenced paramter is busy */
	struct shared *next;
	struct sharedpar *pars;
	char name[2];
};

struct sharedpar {
	struct iopar iopar;
	struct shared *master;
	struct sharedpar *next;
	int icontribute;
};

/* main set function */
static int set_shared(struct iopar *iopar, double newvalue)
{
	struct sharedpar *spar = (struct sharedpar *)iopar, *apar;
	int icontribute = 0;

	if (isnan(newvalue)) {
		if (!spar->icontribute)
			return 0;
		spar->icontribute = 0;
		if (--spar->master->busycnt)
			/* still active clients: current value must stay */
			return 0;

		/* no active clients anymore, return to default (0) */
		newvalue = 0;
	} else {
		icontribute = 1;
		/* I contribute */
		if (spar->master->busycnt - spar->icontribute) {
			/* other clients have control too, don't change the value! */
			if (fabs((newvalue - get_iopar(spar->master->refpar, 0))/newvalue) > 0.001)
				/* busy */
				return -1;
		}
	}

	if (set_iopar(spar->master->refpar, newvalue) < 0)
		return -1;
	/* update myself */
	spar->iopar.value = newvalue;

	if (icontribute && !spar->icontribute) {
		/* add myself as contributing (active) client */
		spar->icontribute = 1;
		++spar->master->busycnt;
	}
	/* synchronize all clients */
	for (apar = spar->master->pars; apar; apar = apar->next) {
		if (apar == spar)
			continue;
		apar->iopar.value = newvalue;
		iopar_set_dirty(&apar->iopar);
	}

	return 0;
}

/* global data */
static struct shared *table;

static void del_shared(struct iopar *iopar)
{
	struct sharedpar *spar = (void *)iopar, **ppar;

	/*
	 * Don't contribute any longer
	 * If I were the last contributor, master will revert refpar to default
	 */
	set_shared(iopar, FP_NAN);

	/* remove myself from master */
	for (ppar = &spar->master->pars; *ppar; ppar = &(*ppar)->next) {
		if (*ppar == spar) {
			*ppar = spar->next;
			break;
		}
	}
	--spar->master->refcnt;

	/* test for the master's use */
	if (!spar->master->refcnt) {
		struct shared **pshared;

		/* remove master from table */
		for (pshared = &table; *pshared; pshared = &(*pshared)->next) {
			if (*pshared == spar->master)
				*pshared = spar->master->next;
		}
		destroy_iopar(spar->master->refpar);
		/* free master, refpar has reverted to default */
		free(spar->master);
	}

	cleanup_libiopar(&spar->iopar);
	free(spar);
}

struct iopar *mkshared(const char *cstr)
{
	struct sharedpar *spar;
	struct shared *shared;

	/* lookup shared */
	for (shared = table; shared; shared = shared->next) {
		if (!strcmp(shared->name, cstr))
			break;
	}
	if (!shared) {
		/* create new */
		shared = zalloc(sizeof(*shared) + strlen(cstr));
		strcpy(shared->name, cstr);
		shared->refpar = create_iopar(cstr);
		if (shared->refpar < 0)
			goto fail_refpar;
		/* register shared in table */
		shared->next = table;
		table = shared;
	}

	spar = zalloc(sizeof(*spar));
	spar->master = shared;
	++shared->refcnt;
	/* register par in shared */
	spar->next = shared->pars;
	shared->pars = spar;

	return &spar->iopar;

fail_refpar:
	free(shared);
	return NULL;
}
