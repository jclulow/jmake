

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

typedef enum parse_state_id {
	PARSE_ERROR = -1,
	PARSE_DONE = 0,
	PARSE_REST = 1,
	PARSE_COMMAND,
	PARSE_EXPANSION_SIMPLE,
	PARSE_EXPANSION_FULL,
	PARSE_READ_UNTIL_SEP,
	PARSE_INCLUDE
} parse_state_id_t;

typedef struct parse_state {
	parse_state_id_t ps_id;
	parse_state_id_t ps_previd;

	char ps_buf[MAXLINE];
	char *ps_bufpos;

	make_line_t *ps_makeline;
	char *ps_pos;

	char *ps_scope;
	char *ps_left;
	char *ps_right;
	int ps_parens;
} parse_state_t;


typedef void (*parse_handler_t)(parse_state_t *);

static void hdlr_rest(parse_state_t *);
static void hdlr_cmd(parse_state_t *);
static void hdlr_read_expansion(parse_state_t *);
static void hdlr_read_expansion_full(parse_state_t *);
static void hdlr_read_until_sep(parse_state_t *);
static void hdlr_include(parse_state_t *);

static parse_handler_t hdlrs[] = {
	NULL,				/* PARSE_DONE */
	hdlr_rest,			/* PARSE_REST */
	hdlr_cmd,			/* PARSE_COMMAND */
	hdlr_read_expansion, 		/* PARSE_EXPANSION_SIMPLE */
	hdlr_read_expansion_full,	/* PARSE_EXPANSION_FULL */
	hdlr_read_until_sep,		/* PARSE_READ_UNTIL_SEP */
	hdlr_include			/* PARSE_INCLUDE */
};


/*
 * Parse Buffer Handling Functions:
 */
static void
parse_buf_reset(parse_state_t *ps)
{
	ps->ps_bufpos = ps->ps_buf;
	*(ps->ps_bufpos) = '\0';
}

static void
parse_buf_putc(parse_state_t *ps, char c)
{
	if ((ps->ps_bufpos - ps->ps_buf) + 1 >= sizeof (ps->ps_buf)) {
		fprintf(stderr, "buffer overflow (sz %d)\n",
		    ps->ps_bufpos - ps->ps_buf) + 1;
		abort();
	}
	*(ps->ps_bufpos++) = c;
	*(ps->ps_bufpos) = '\0';
}

static char *
parse_buf_commit(parse_state_t *ps)
{
	return (strdup(ps->ps_buf));
}

/*
 * Parse State Change Functions:
 */
static void
parse_pop_state(parse_state_t *ps)
{
	ps->ps_id = ps->ps_previd;
	ps->ps_previd = PARSE_ERROR;
}

static void
parse_push_state(parse_state_t *ps, parse_state_id_t id)
{
	if (ps->ps_previd != PARSE_ERROR)
		abort();
	ps->ps_previd = ps->ps_id;
	ps->ps_id = id;
}

/*
 * Main Entry Point:
 */
int
parse_line(make_line_t *ml)
{
	parse_state_t ps;

	bzero(&ps, sizeof (ps));

	ps.ps_previd = PARSE_ERROR;
	ps.ps_id = PARSE_REST;
	ps.ps_makeline = ml;
	ps.ps_pos = ml->ml_line;

	for (;;) {
		if (ps.ps_id < 0) {
			int i;
			/*
			 * ERROR!
			 */
			fprintf(stderr, "parse error\n");
			fprintf(stderr, "[%s:%d-%d]:\n%s\n", ml->ml_file, ml->ml_linemin,
			    ml->ml_linemax, ml->ml_line);
			for (i = 0; i < ps.ps_pos - ml->ml_line; i++)
				fprintf(stderr, " ");
			fprintf(stderr, "^\n");
			return (-1);
		} else if (ps.ps_id == PARSE_DONE) {
			return (0);
		}
		hdlrs[ps.ps_id](&ps);
	}
}

/*
 * Per-State Parse Handlers:
 */
static void
hdlr_rest(parse_state_t *ps)
{
	char c = *(ps->ps_pos);

	if (c == '\0' || c == '#') {
		/*
		 * Empty line.
		 */
		ps->ps_id = PARSE_DONE;
	} else if (c == '\t') {
		/*
		 * Commands to execute.
		 */
		ps->ps_pos++;
		ps->ps_id = PARSE_COMMAND;
	} else {
		ps->ps_id = PARSE_READ_UNTIL_SEP;
		parse_buf_reset(ps);
	}
}

static void
hdlr_cmd(parse_state_t *ps)
{
	add_make_command(ps->ps_makeline, ps->ps_pos);
	ps->ps_id = PARSE_DONE;
}

static void
hdlr_read_until_sep(parse_state_t *ps)
{
	char c = *(ps->ps_pos);
	char cc = c == '\0' ? '\0' : *(ps->ps_pos + 1);

	if (c == '\0' || c == '#') {
		/*
		 * XXX End of line before separator?
		 */
		if (strstr(ps->ps_buf, "include ") == ps->ps_buf) {
			//ps->ps_pos += strlen("include ");
			add_make_include(ps->ps_makeline, ps->ps_buf + strlen("include "));
			ps->ps_id = PARSE_DONE;
		} else {
			fprintf(stderr, "unexpected EOL\n");
			ps->ps_id = PARSE_ERROR;
		}
	} else if (c == ':' && cc == '=') {
		/*
		 * Conditional Macro.
		 *
		 * Stash the scope and go around again to find the macro itself.
		 */
		ps->ps_pos += 2;
		ps->ps_scope = parse_buf_commit(ps);
		parse_buf_reset(ps);
	} else if (c == ':') {
		/*
		 * Target.
		 */
		if (ps->ps_scope != NULL)
			abort();

		ps->ps_pos++;
		add_make_target(ps->ps_makeline, ps->ps_buf, ps->ps_pos);

		ps->ps_id = PARSE_DONE;
	} else if (c == '=') {
		/*
		 * Macro.
		 */
		ps->ps_pos++;
		add_make_macro(ps->ps_makeline, ps->ps_scope, ps->ps_buf,
		    ps->ps_pos, B_FALSE);

		ps->ps_id = PARSE_DONE;
	} else if (c == '+' && cc == '=') {
		/*
		 * Append Macro.
		 */
		ps->ps_pos++;
		add_make_macro(ps->ps_makeline, ps->ps_scope, ps->ps_buf,
		    ps->ps_pos, B_TRUE);

		ps->ps_id = PARSE_DONE;
	} else if (c == '$') {
		/*
		 * A Macro Expansion.
		 */
		parse_push_state(ps, PARSE_EXPANSION_SIMPLE);
	} else {
		/*
		 * Keep Reading...
		 */
		parse_buf_putc(ps, c);
		ps->ps_pos++;
	}
}


static void
hdlr_read_expansion(parse_state_t *ps)
{
	char c = *(ps->ps_pos);
	char cc = c == '\0' ? '\0' : *(ps->ps_pos + 1);

	if (c == '\0' || c == '#' || cc == '\0' || cc == '#') {
		/*
		 * XXX End of line?!
		 */
		fprintf(stderr, "unexpected EOL\n");
		ps->ps_id = PARSE_ERROR;
	} else if (c == '$' && cc == '(') {
		/*
		 * A parenthetical, and thus 'full', expansion.
		 * Eat the dollar and hand on.
		 */
		parse_buf_putc(ps, c);
		ps->ps_pos++;
		ps->ps_id = PARSE_EXPANSION_FULL;
		ps->ps_parens = 0;
	} else if (c == '$') {
		/*
		 * Simple single-character expansion.
		 */
		if (c == '@') {
			parse_buf_putc(ps, c);
			parse_pop_state(ps);
			ps->ps_pos++;
		} else {
			fprintf(stderr, "invalid expansion: %c\n", cc);
			ps->ps_id = PARSE_ERROR;
		}
	} else {
		abort();
	}
}

static void
hdlr_read_expansion_full(parse_state_t *ps)
{
	char c = *(ps->ps_pos);

	if (c == '\0' || c == '#') {
		/*
		 * XXX End of line?!
		 */
		fprintf(stderr, "unexpected EOL\n");
		ps->ps_id = PARSE_ERROR;
		return;
	} else if (c == '(') {
		ps->ps_parens++;
	} else if (c == ')') {
		if (--ps->ps_parens == 0) {
			/*
			 * End of expansion.
			 */
			parse_pop_state(ps);
		}
	}
	parse_buf_putc(ps, c);
	ps->ps_pos++;
}

static void
hdlr_include(parse_state_t *ps)
{
	if (strstr(ps->ps_pos, "include ") == ps->ps_pos) {
		ps->ps_pos += strlen("include ");
		add_make_include(ps->ps_makeline, ps->ps_pos);
		ps->ps_id = PARSE_DONE;
	} else {
		fprintf(stderr, "expected 'include'\n");
		ps->ps_id = PARSE_ERROR;
	}
}

/* vim: set ts=8 tw=80 noet sw=8 */
