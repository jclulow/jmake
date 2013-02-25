

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


static make_line_t *add_make_line(make_file_t *, int, char *);
static make_file_t *read_makefile(const char *path);
static int fold_lines(make_file_t *mf);
static int parse_lines_from_file(make_file_t *);


static make_line_t *
add_make_line(make_file_t *mf, int lineno, char *line)
{
	make_line_t *ml = calloc(1, sizeof (*ml));

	ml->ml_file = mf;
	ml->ml_linemin = ml->ml_linemax = lineno;
	ml->ml_line = line;

	TAILQ_INSERT_TAIL(&mf->mf_lines, ml, ml_linkage);

	return (ml);
}

static make_file_t *
read_makefile(const char *path)
{
	FILE *fp = NULL;
	int linecount = 0;
	char *linebuf = calloc(1, MAXLINE);
	make_file_t *mf = calloc(1, sizeof (*mf));

	TAILQ_INIT(&mf->mf_lines);
	mf->mf_path = strdup(path);

	if ((fp = fopen(path, "r")) == NULL) {
		fprintf(stderr, "Could not open file '%s': %s\n",
		    path, strerror(errno));
		goto reterr;
	}

	for (;;) {
		char *lf = NULL;
		char *str = fgets(linebuf, MAXLINE, fp);
		linecount++;

		if (str == NULL) {
			if (ferror(fp)) {
				fprintf(stderr, "Could not read file '%s':"
				    " %s\n", path, strerror(errno));
				goto reterr;
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

		(void) add_make_line(mf, linecount, strdup(linebuf));
	}

	fold_lines(mf);

	return (mf);

reterr:
	if (linebuf != NULL)
		free(linebuf);
	if (mf != NULL) {
		if (mf->mf_path != NULL)
			free(mf->mf_path);
		free(mf);
	}
	if (fp != NULL)
		(void) fclose(fp);
	return (NULL);
}

static int
fold_lines(make_file_t *mf)
{
	make_line_t *ml, *lm, *join = NULL;

	for (ml = TAILQ_FIRST(&mf->mf_lines); ml != NULL; ml = lm) {
		int len;
		lm = TAILQ_NEXT(ml, ml_linkage);

		if (join != NULL) {
			char *tmp = NULL;
			/*
			 * Previous line had a continuation, so
			 * join it to this one.
			 */
			(void) asprintf(&tmp, "%s %s", join->ml_line,
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
			TAILQ_REMOVE(&mf->mf_lines, ml, ml_linkage);
			join = ml;
		}
	}

	if (join != NULL)
		TAILQ_INSERT_TAIL(&mf->mf_lines, ml, ml_linkage);

	return (0);
}

void
dump_make_file(make_file_t *mf)
{
	make_line_t *ml;
	TAILQ_FOREACH(ml, &mf->mf_lines, ml_linkage) {
		fprintf(stderr, "[%s:%d] %s\n", ml->ml_file->mf_path,
		    ml->ml_linemin, ml->ml_line);
	}
}

static int
parse_lines_from_file(make_file_t *mf)
{
	make_line_t *ml;

	TAILQ_FOREACH(ml, &mf->mf_lines, ml_linkage) {
		char *read_path = NULL;
		if (parse_line(ml, &read_path) != 0)
			return (-1);

		if (read_path != NULL) {
			/*
			 * We need to read in another file, e.g. because of
			 * an 'include' directive.
			 *
			 * XXX we should attach this make_file_t to the whole
			 * connected structure somehow.
			 *
			 * XXX maybe make this not recursive ffs.
			 */
			make_file_t *subfile;
		       
			if ((subfile = read_makefile(read_path)) == NULL) {
				fprintf(stderr, "could not include makefile "
				    "%s\n", read_path);
				return (-1);
			}

			if (parse_lines_from_file(subfile) != 0)
				return (-1);
		}
	}
}

int
start(char *path)
{
	make_file_t *top;

	/*
	 * Read in lines from the first Makefile:
	 */
	if ((top = read_makefile(path)) == NULL) {
		fprintf(stderr, "could not read makefile %s\n", path);
		return (-1);
	}

	return (parse_lines_from_file(top));
}

/* vim: set ts=8 tw=80 noet sw=8 */
