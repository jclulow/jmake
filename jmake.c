

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/sysmacros.h>
#include <sys/ctype.h>

#include "jmake.h"
#include "ents.h"


#define	MAXLINE	4096

TAILQ_HEAD(make_line_head, make_line) make_lines =
    TAILQ_HEAD_INITIALIZER(make_lines);


static make_line_t *
add_make_line(char *file, int lineno, char *line)
{
	make_line_t *ml = calloc(1, sizeof (*ml));

	ml->ml_file = file;
	ml->ml_linemin = ml->ml_linemax = lineno;
	ml->ml_line = line;

	TAILQ_INSERT_TAIL(&make_lines, ml, ml_linkage);

	return (ml);
}

static int
parse_makefile(char *path)
{
	char *ourpath = strdup(path);
	char linebuf[MAXLINE];
	FILE *mf;
	int linecount = 0;

	if ((mf = fopen(path, "r")) == NULL) {
		fprintf(stderr, "Could not open file '%s': %s\n",
		    path, strerror(errno));
		return (-1);
	}


	for (;;) {
		char *lf = NULL;
		char *str = fgets(linebuf, sizeof (linebuf), mf);
		linecount++;

		if (str == NULL) {
			if (ferror(mf)) {
				fprintf(stderr, "Could not read file '%s':"
				    " %s\n", path, strerror(errno));
				(void) fclose(mf);
				return (-1);
			} else {
				/*
				 * EOF, so we're done.
				 */
				break;
			}
		}

		/*
		 * Remove trailing \n if extant:
		 */
		if ((lf = strchr(linebuf, '\n')) != NULL)
			*lf = '\0';

		(void) add_make_line(ourpath, linecount, strdup(linebuf));
	}
}

static int
fold_lines(void)
{
	make_line_t *ml, *lm, *join = NULL;

	for (ml = TAILQ_FIRST(&make_lines); ml != NULL; ml = lm) {
		int len;
		lm = TAILQ_NEXT(ml, ml_linkage);

		if (join != NULL) {
			char *tmp = NULL;
			/*
			 * Previous line had a continuation, so
			 * join it to this one.
			 */
			(void) asprintf(&tmp, "%s%s", join->ml_line,
			    ml->ml_line);

			free(ml->ml_line);
			ml->ml_line = tmp;

			ml->ml_linemin = MIN(ml->ml_linemin,
			    join->ml_linemin);
			ml->ml_linemax = MAX(ml->ml_linemax,
			    join->ml_linemax);

			free(join->ml_line);
			free(join);
			join = NULL;
		}

		len = strlen(ml->ml_line);
		if (len < 1)
			continue;
		if (ml->ml_line[len - 1] == '\\') {
			/*
			 * This line has a continuation, so remove the
			 * trailing backslash and remove the line from
			 * the list.  We store it in 'join' for next time
			 * around the loop.
			 */
			ml->ml_line[len - 1] = '\0';
			TAILQ_REMOVE(&make_lines, ml, ml_linkage);
			join = ml;
		}
	}

	if (join != NULL)
		TAILQ_INSERT_TAIL(&make_lines, ml, ml_linkage);

	return (0);
}

void
dump_make_file()
{
	make_line_t *ml;
	TAILQ_FOREACH(ml, &make_lines, ml_linkage) {
		fprintf(stderr, "[%s:%d] %s\n", ml->ml_file,
		    ml->ml_linemin, ml->ml_line);
	}
}

static int
awesome()
{
	make_line_t *ml;

	TAILQ_FOREACH(ml, &make_lines, ml_linkage) {
		//if (awesome_line(ml) != 0)
		if (parse_line(ml) != 0)
			return (-1);
	}

	fprintf(stderr, "\n\n");

	dump_ents();
}

int
main(int argc, char **argv)
{
	int err;

	/*
	 * XXX Parse options:
	 */

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <Makefile>\n", argv[0]);
		return (1);
	}

	if ((err = parse_makefile(argv[1])) != 0) {
		fprintf(stderr, "Parse failed.\n");
		return (err);
	}

	fold_lines();

	awesome();

	return (0);
}

/* vim: set ts=8 tw=80 noet sw=8 */
