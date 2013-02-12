

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


#define	MAXLINE	4096


TAILQ_HEAD(make_line_head, make_line) make_lines =
    TAILQ_HEAD_INITIALIZER(make_lines);

typedef struct make_line {
	char *ml_file;
	int ml_linemin;
	int ml_linemax;

	char *ml_line;

	TAILQ_ENTRY(make_line) ml_linkage;

} make_line_t;

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

typedef enum line_parse_state {
	LPS_REST = 1,
	LPS_MACRO_OR_TARGET = 2,
	LPS_MOT_WAIT_OPERATOR = 3,
	LPS_MOT_EQUALS = 4,
	LPS_MOT_TARGET_OR_CONDMACRO = 5
} line_parse_state_t;


#define	LINE_ERROR(ml, fmt, ...) 	\
	(void) fprintf(stderr, "ERROR: %u [%s:%d-%d] " fmt "\n", lps, \
	    ml->ml_file, ml->ml_linemin, ml->ml_linemax, ##__VA_ARGS__)

static int
awesome_line(make_line_t *ml)
{
	char tmp[MAXLINE], *tpos;
	char *pos;
	line_parse_state_t lps;

	char *id0 = NULL, *id1 = NULL, *val0 = NULL;

	/*
	 * Let's go for a walk through the line:
	 */
	pos = ml->ml_line;
	lps = LPS_REST;
	for (;;) {
		char c = *pos++;

		/*
		 * We've hit a comment, so make like it's the end of the
		 * line.
		 */
		if (c == '#')
			c = '\0';

		switch (lps) {
		case LPS_REST:
			if (c == '\0') {
				/*
				 * Empty line.
				 */
				LINE_ERROR(ml, "empty line");
				return (0);
			} else if (c == '\t') {
				/*
				 * Commands to execute.
				 */
				LINE_ERROR(ml, "commands '%s'", pos);
				return (0);
			} else if (ISSPACE(c)) {
				break;
			} else if (ISALNUM(c) || c == '_' || c == '-') {
				tpos = tmp;
				*tpos++ = c;
				lps = LPS_MACRO_OR_TARGET;
				break;
			} else {
				LINE_ERROR(ml, "unexpected character '%c'", c);
				return (-1);
			}
			break;

		case LPS_MACRO_OR_TARGET:
			if (c == '\0') {
				LINE_ERROR(ml, "unexpected end of line");
				return (-1);
			} else if (ISALNUM(c) || c == '_' || c == '-') {
				*tpos++ = c;
				break;
			} else {
				*tpos = '\0';
				id0 = strdup(tmp);
				if (ISSPACE(c)) {
					lps = LPS_MOT_WAIT_OPERATOR;
				} else if (c == '=') {
					lps = LPS_MOT_EQUALS;
				} else if (c == ':') {
					if (id1 != NULL) {
						/*
						 * XXX this is a condmacro
						 * so abort
						 */
						LINE_ERROR(ml, "condmacro "
						     "... OH GOD");
					}
					lps = LPS_MOT_TARGET_OR_CONDMACRO;
				} else {
					LINE_ERROR(ml, "unexpected character"
					    " '%c'", c);
					return (-1);
				}
				break;
			}
			break;

		case LPS_MOT_WAIT_OPERATOR:
			if (c == '\0') {
				LINE_ERROR(ml, "unexpected end of line");
				return (-1);
			} else if (ISSPACE(c)) {
				break;
			} else if (c == ':') {
				lps = LPS_MOT_TARGET_OR_CONDMACRO;
				break;
			} else if (c == '=') {
				lps = LPS_MOT_EQUALS;
				break;
			} else if (c == '+') {
				if (*pos == '=') {
					pos++;
					if (id1 != NULL)
						LINE_ERROR(ml, "cond macro"
						    " '%s'", id1);
					LINE_ERROR(ml, "macro append %s", id0);
					LINE_ERROR(ml, "value: %s", pos);
					return (0);
				} else {
					LINE_ERROR(ml, "unexpected character"
					     " '%c'", c);
					return (-1);
				}
			} else {
				if (strcmp(id0, "include") == 0) {
					LINE_ERROR(ml, "include: %c%s", c, pos);
					return (0);
				}
				/* XXX ? */
				LINE_ERROR(ml, "unexpected character '%c'", c);
				return (-1);
			}

		case LPS_MOT_EQUALS:
			if (c == '\0') {
				/*
				 * Empty Macro.
				 */
				if (id1 != NULL)
					LINE_ERROR(ml, "cond macro '%s'", id1);
				LINE_ERROR(ml, "empty macro '%s'", id0);
				return (0);
			} else if (ISSPACE(c)) {
				if (id1 != NULL)
					LINE_ERROR(ml, "cond macro '%s'", id1);
				LINE_ERROR(ml, "macro '%s'", id0);
				LINE_ERROR(ml, "value '%s'", pos);
				return (0);
			} else {
				LINE_ERROR(ml, "unexpected character '%c'", c);
				return (-1);
			}

		case LPS_MOT_TARGET_OR_CONDMACRO:
			if (c == '\0') {
				/*
				 * Target with no dependencies.
				 */
				LINE_ERROR(ml, "nodeps target '%s'", id0);
				return (0);
			} else if (c == '=') {
				/*
				 * Conditional Macro.
				 */
				id1 = id0;
				id0 = NULL;
				lps = LPS_REST;
				break;
			} else {
				/*
				 * Target with dependencies.
				 */
				LINE_ERROR(ml, "target '%s'", id0);
				LINE_ERROR(ml, "deps '%s'", pos);
				return (0);
			}
			break;

		}
	}

	return (0);
}

static int
awesome()
{
	make_line_t *ml;

	TAILQ_FOREACH(ml, &make_lines, ml_linkage) {
		if (awesome_line(ml) != 0)
			return (-1);
	}
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
	dump_make_file();

	awesome();

	return (0);
}

/* vim: set ts=8 tw=80 noet sw=8 */
