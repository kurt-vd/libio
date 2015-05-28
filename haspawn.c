#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include <unistd.h>
#include <error.h>
#include <getopt.h>
#include <sys/wait.h>

#include "libio.h"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": spawn process on input\n"
	"Usage: " NAME " [OPTIONS] <PARAM> PROGRAM [ARGS]\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"

	" -i, --immediate	Trigger immediately (like btndown)\n"
	" -s, --short		Trigger on short press\n"
	" -d, --delay=SECS	detect long press\n"
	;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "immediate", no_argument, NULL, 'i', },
	{ "short", no_argument, NULL, 's', },
	{ "delay", required_argument, NULL, 'd', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "+?Vvisd:";

static struct args {
	int verbose;
	int type;
	double delay;
} s = {
	.type = LONGPRESS,
	.delay = 0.5,
};

static void sigchld(int sig)
{
	int pid, status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		elog(LOG_INFO, 0, "pid %u exited", pid);

	signal(sig, sigchld);
}

static void starttorun(char **argv)
{
	int pid;

	pid = fork();
	if (!pid) {
		elog(LOG_NOTICE, 0, "forked %u", getpid());
		execvp(*argv, argv);
		elog(LOG_CRIT, errno, "execvp %s ...", *argv);
	} else if (pid < 0)
		elog(LOG_CRIT, errno, "fork");
}

static int haspawn(int argc, char *argv[])
{
	int opt, param, ldid;

	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, LOCALVERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 'i':
		s.type = 0;
		break;
	case 's':
		s.type = SHORTPRESS;
		break;
	case 'd':
		s.delay = strtod(optarg, NULL);
		break;
	case '?':
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	libio_set_trace(s.verbose);

	if (optind +2 > argc) {
		fputs(help_msg, stderr);
		exit(1);
	}

	param = create_iopar(argv[optind++]);
	if (s.type)
		ldid = new_longdet1(s.delay);
	else
		/* keep elder compiler happy */
		ldid = -1;

	/* main ... */
	signal(SIGCHLD, sigchld);
	while (1) {
		if (s.type) {
			set_longdet(ldid, get_iopar(param));
			if (longdet_edge(ldid) && (longdet_state(ldid) == s.type))
				starttorun(argv+optind);
		} else if (iopar_dirty(param) && (get_iopar(param) > 0.5))
			starttorun(argv+optind);

		/* enter sleep */
		if (libio_wait() < 0)
			break;
	}
	return 0;
}

__attribute__((constructor))
static void init(void)
{
	register_applet(NAME, haspawn);
}
