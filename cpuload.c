#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <math.h>

#include <getopt.h>

#include "lib/libt.h"
#include "_libio.h"

/* create an index that is not -1, but is way beyond a possible #cpus */
#define ALLCPU 0x100000

/* structures */
struct load {
	unsigned long user   ;
	unsigned long nice   ;
	unsigned long system ;
	unsigned long idle   ;
	unsigned long iowait ;
	unsigned long irq    ;
	unsigned long softirq;
};

struct cpu {
	struct cpu *next;
	int index;

	/* parameters */
	double load, wait;
	int present;

	/* cache */
	struct load state;
};

struct cpupar {
	struct iopar iopar;
	int index;
	double (*extract)(struct cpu *);
	struct cpupar *next;
	struct cpupar *prev;
};

/* global variables */
static struct cpu *cpus, *lastcpu;
static struct cpupar *cpupars;

__attribute__((destructor))
static void free_cpus(void)
{
	struct cpu *cpu;

	while (cpus) {
		cpu = cpus;
		cpus = cpus->next;

		free(cpu);
	}
	cpus = lastcpu = NULL;
}

/* parse /proc/ cpuload */
static int parse_cpu_load(const char *line, struct load *load)
{
	char *p;
	int index;

	if (strncmp(line, "cpu", 3))
		return -1;
	if (line[3] == ' ') {
		index = ALLCPU;
		p = (char *)line+4;
	} else
		index = strtoul(line+3, &p, 10);

	load->user = strtol(p, &p, 0);
	load->nice = strtol(p, &p, 0);
	load->system = strtol(p, &p, 0);
	load->idle = strtol(p, &p, 0);
	load->iowait = strtol(p, &p, 0);
	load->irq = strtol(p, &p, 0);
	load->softirq = strtol(p, &p, 0);
	return index;
}

static struct cpu *add_cpu(int index, const struct load *initial_load)
{
	struct cpu *cpu;

	/* create new entry */
	cpu = zalloc(sizeof(*cpu));
	cpu->index = index;
	cpu->state = *initial_load;
	cpu->load = cpu->wait = NAN;
	/* add in linked list */
	if (lastcpu)
		lastcpu->next = cpu;
	else
		cpus = cpu;
	lastcpu = cpu;
	return cpu;
}

/*
 * cpu load merge
 * Here, new values are provided, and this function
 * will expose new parameter values
 */
static void cpu_merge_load(struct cpu *cpu, const struct load *load)
{
	double total, busy, iow;

	/* make diffs */
	busy = load->user - cpu->state.user +
		load->nice - cpu->state.nice +
		load->system - cpu->state.system +
		load->irq - cpu->state.irq +
		load->softirq - cpu->state.softirq;
	iow = load->iowait - cpu->state.iowait;
	total = busy + iow + load->idle - cpu->state.idle;

	/* save new state */
	cpu->state = *load;

	/* export values */
	cpu->load = busy / total;
	cpu->wait = iow / total;
}

/* Value extractors aka types */
static double extract_cpu_load(struct cpu *cpu)
{
	return cpu->load;
}

static double extract_cpu_wait(struct cpu *cpu)
{
	return cpu->wait;
}

static void cpu_timer(void *data)
{
	int ret, index, count;
	struct cpu *cpu;
	struct cpupar *cp;
	FILE *fp;
	char *line = NULL;
	size_t linesize = 0;
	struct load load;

	for (cpu = cpus; cpu; cpu = cpu->next)
		cpu->present = 0;

	/* reset new found #cpu */
	count = 0;
	/* read values */
	fp = fopen("/proc/stat", "r");
	if (!fp)
		elog(LOG_CRIT, errno, "fopen /proc/stat");
	for (;;) {
		ret = getline(&line, &linesize, fp);
		if (ret <= 0)
			break;
		/* parse */
		index = parse_cpu_load(line, &load);
		if (index < 0)
			continue;
		for (cpu = cpus; cpu; cpu = cpu->next)
			if (cpu->index == index)
				break;
		if (!cpu) {
			/* register new cpu */
			add_cpu(index, &load);
			if (index != ALLCPU)
				++count;
		} else {
			/* deal with cpu */
			cpu_merge_load(cpu, &load);
			cpu->present = 1;
		}
	}
	/* cleanup */
	if (line)
		free(line);
	fclose(fp);
	/* update parameters */
	for (cp = cpupars; cp; cp = cp->next) {
		/* lookup corresponding cpu */
		for (cpu = cpus; cpu; cpu = cpu->next) {
			if (cpu->index == cp->index)
				break;
		}
		cp->iopar.value = cpu ? cp->extract(cpu) : NAN;
		if (!isnan(cp->iopar.value))
			iopar_set_dirty(&cp->iopar);
		if (cpu && cpu->present)
			iopar_set_present(&cp->iopar);
		else
			iopar_clr_present(&cp->iopar);
	}

	/* schedule next */
	libt_add_timeout(1, cpu_timer, data);
}

/* CPU parameters */
static void del_cpupar(struct iopar *iopar)
{
	struct cpupar *cp = (void *)iopar;

	/* remove from linked list */
	if (cp->prev)
		cp->prev->next = cp->next;
	else
		cp = cp->next;
	if (cp->next)
		cp->next->prev = cp->prev;

	/* remove timer on last cpu */
	if (!cpupars)
		libt_remove_timeout(cpu_timer, NULL);
	cleanup_libiopar(&cp->iopar);
	free(cp);
}

struct iopar *mkcpupar(char *desc)
{
	struct cpupar *cp;

	cp = zalloc(sizeof(*cp));
	cp->iopar.del = del_cpupar;
	cp->iopar.set = NULL;
	/* force the first read to mark value as dirty */
	cp->iopar.value = NAN;

	if (!strncmp(desc, "load", 4)) {
		cp->extract = extract_cpu_load;
		cp->index = desc[4] ? strtoul(desc+4, NULL, 0) : ALLCPU;
	} else if (!strncmp(desc, "wait", 4)) {
		cp->extract = extract_cpu_wait;
		cp->index = desc[4] ? strtoul(desc+4, NULL, 0) : ALLCPU;
	} else
		elog(LOG_CRIT, 0, "bad type %s", desc);

	/* put in linked list */
	cp->next = cpupars;
	cpupars = cp;

	/* trigger first read and start timer */
	if (!cp->next)
		cpu_timer(NULL);

	return &cp->iopar;
}
