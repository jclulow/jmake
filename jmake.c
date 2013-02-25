

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <sys/ctype.h>

#include "jmake.h"
#include "ents.h"


static int eflag = 0;

char *
get_from_environ(const char *name)
{
	char *out;

	if (eflag == 0)
		return (NULL);

	out = getenv(name);
	return (out == NULL ? NULL : strdup(out));
}

int
main(int argc, char **argv)
{
	int err;
	char *fflag = NULL, *xflag = NULL;
	int c;

	while ((c = getopt(argc, argv, ":f:eX:")) != -1) {
		switch (c) {
		case 'e':
			eflag++;
			break;
		case 'f':
			fflag = optarg;
			break;
		case 'X':
			xflag = optarg;
			break;
		case ':':
			fprintf(stderr, "option -%c requires an operand\n",
			     optopt);
			exit(1);
			break;
		case '?':
			fprintf(stderr, "unknown option -%c\n", optopt);
			exit(1);
			break;
		}
	}

	if (fflag == NULL) {
		fprintf(stderr, "Usage: %s -f <Makefile> ...\n", argv[0]);
		return (1);
	}

	if ((err = start(fflag)) != 0) {
		fprintf(stderr, "Parse failed.\n");
		return (err);
	}

	dump_ents();

	for (; optind < argc; optind++) {
		dump_cmd_for_target(argv[optind]);
	}

	if (xflag != NULL) {
		dump_macro(NULL, xflag);
	}

	return (0);
}

/* vim: set ts=8 tw=80 noet sw=8 */
