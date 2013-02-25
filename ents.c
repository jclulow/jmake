

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


#include "ents.h"


#define	MAXLINE	4096

typedef TAILQ_HEAD(make_ent_head, make_ent) make_ent_head_t;

make_ent_head_t make_ents = TAILQ_HEAD_INITIALIZER(make_ents);

#if 0
TAILQ_HEAD(make_ent_head, make_ent) make_ents =
    TAILQ_HEAD_INITIALIZER(make_ents);
#endif

typedef enum make_ent_type {
	ENT_MACRO = 1,
	ENT_TARGET,
	ENT_COMMAND,
	ENT_INCLUDE,
	ENT_DEP
} make_ent_type_t;

#define	MAKE_ENT_FIELDS \
	make_ent_type_t me_type; \
	make_line_t *me_line; \
	TAILQ_ENTRY(make_ent) me_linkage


typedef struct make_ent {
	MAKE_ENT_FIELDS;
} make_ent_t;

typedef struct make_ent_include {
	MAKE_ENT_FIELDS;

	char *me_path;
} make_ent_include_t;

typedef struct make_ent_command {
	MAKE_ENT_FIELDS;

	char *me_command;
} make_ent_command_t;

typedef struct make_ent_dep {
	MAKE_ENT_FIELDS;

	char *me_name;
} make_ent_dep_t;

typedef struct make_ent_macro {
	MAKE_ENT_FIELDS;

	char *me_scope;

	char *me_name;
	char *me_value;

	boolean_t me_reset_value;

} make_ent_macro_t;

typedef struct make_ent_target {
	MAKE_ENT_FIELDS;

	char *me_name;

	make_ent_head_t me_deps;
	make_ent_head_t me_commands;
} make_ent_target_t;

#define	TO_ENT(x)		((make_ent_t *) (x))
#define	TO_MACRO(x)		((make_ent_macro_t *) (x))
#define	TO_TARGET(x)		((make_ent_target_t *) (x))
#define	TO_COMMAND(x)		((make_ent_command_t *) (x))
#define	TO_INCLUDE(x)		((make_ent_include_t *) (x))
#define	TO_DEP(x)		((make_ent_dep_t *) (x))

#define	MAX_TARGETS_ON_LINE	256
static make_ent_target_t *last_targets[MAX_TARGETS_ON_LINE];
static int nlast_targets = 0;

static void
reset_last_target()
{
	nlast_targets = 0;
}

static void
add_last_target(make_ent_target_t *t)
{
	if (nlast_targets >= MAX_TARGETS_ON_LINE) {
		fprintf(stderr, "too many targets on line\n");
		exit(1);
	}
	last_targets[nlast_targets++] = t;
}

static void
dump_ent_heading(char *type, make_line_t *ml)
{
	fprintf(stderr, "----------------\n%s\t(%s:%d-%d)\n", type, ml->ml_file,
	    ml->ml_linemin, ml->ml_linemax);
}

static void
dump_ent(make_ent_t *t)
{
	switch (t->me_type) {
	case ENT_MACRO: {
		make_ent_macro_t *m = TO_MACRO(t);
		if (m->me_scope == NULL) {
			dump_ent_heading("MACRO", m->me_line);
		} else {
			dump_ent_heading("COND. MACRO", m->me_line);
			fprintf(stderr, "\tscope:%s\n", m->me_scope);
		}
		fprintf(stderr, "\tname:%s\n\tvalue:%s\n",
		    m->me_name, m->me_value);
		fprintf(stderr, "\ttype: %s\n", m->me_reset_value ==
		    B_TRUE ? "+=" : "=");
		break;
	}
	case ENT_TARGET: {
		make_ent_target_t *m = TO_TARGET(t);
		make_ent_t *e;
		dump_ent_heading("TARGET", m->me_line);
		fprintf(stderr, "\tname:%s\n", m->me_name);
		TAILQ_FOREACH(e, &m->me_deps, me_linkage) {
			fprintf(stderr, "\tdep: %s\n", TO_DEP(e)->me_name);
		}
		TAILQ_FOREACH(e, &m->me_commands, me_linkage) {
			fprintf(stderr, "\t\t| %s\n",
			     TO_COMMAND(e)->me_command);
		}
		break;
	}
	case ENT_INCLUDE: {
		make_ent_include_t *m = TO_INCLUDE(t);
		make_ent_t *e;
		dump_ent_heading("INCLUDE", m->me_line);
		fprintf(stderr, "\tpath:%s\n", m->me_path);
		break;
	}
	default:
		fprintf(stderr, "Unknown make_ent type: %d\n", t->me_type);
		exit(1);
	}
}

void
dump_ents()
{
	make_ent_t *me;

	TAILQ_FOREACH(me, &make_ents, me_linkage) {
		dump_ent(me);
	}
}

static make_ent_t *
new_make_ent(make_ent_type_t type, make_line_t *line)
{
	make_ent_t *me;
	size_t size;
	switch (type) {
	case ENT_MACRO:
		size = sizeof (make_ent_macro_t);
		break;
	case ENT_TARGET:
		size = sizeof (make_ent_target_t);
		break;
	case ENT_COMMAND:
		size = sizeof (make_ent_command_t);
		break;
	case ENT_INCLUDE:
		size = sizeof (make_ent_include_t);
		break;
	case ENT_DEP:
		size = sizeof (make_ent_dep_t);
		break;
	default:
		fprintf(stderr, "Unknown make_ent type: %d\n", type);
		exit(1);
	}
	me = calloc(1, size);
	me->me_type = type;
	me->me_line = line;
	return (me);
}

#define	MAX_WORDS	128
typedef struct splitter {
	char *spl_words[MAX_WORDS];
	int spl_nwords;
} splitter_t;

static void
free_splits(splitter_t *spl)
{
	int i;
	for (i = 0; i < spl->spl_nwords; i++)
		free(spl->spl_words[i]);
	spl->spl_nwords = 0;
}

static void
split_into_words(char *instr, splitter_t *spl)
{
	char buf[MAXLINE];
	char *pos, *opos;
	int parens = 0;

	spl->spl_nwords = 0;
	bzero(spl->spl_words, sizeof(spl->spl_words));
	buf[0] = '\0';
	opos = buf;

	for (pos = instr; *pos != '\0'; pos++) {
		char c = *pos;
		char cc = *(pos + 1);

		if (c == ')' && --parens > 0) {
			*opos++ = ')';
		} else if (c == '$' && cc == '(') {
			parens++;
			*opos++ = '$';
			*opos++ = '(';
		} else if (ISSPACE(c) && opos != buf) {
			/*
			 * Commit word to list:
			 */
			*opos = '\0';
			spl->spl_words[spl->spl_nwords++] = strdup(buf);
			opos = buf;
			buf[0] = '\0';
		} else if (ISSPACE(c)) {
			/*
			 * Skip whitespace.
			 */
		} else {
			*opos++ = c;
		}
	}

	if (parens > 0) {
		/* XXX */
		fprintf(stderr, "unterminated macro expansion\n");
		exit(1);
	}

	if (opos != buf) {
		*opos = '\0';
		spl->spl_words[spl->spl_nwords++] = strdup(buf);
	}
}

void
add_make_macro(make_line_t *line, char *scope, char *name, char *value, boolean_t reset_val)
{
	splitter_t spl_scopes, spl_names;
	int i, j;

	if (scope != NULL) {
		split_into_words(scope, &spl_scopes);
	}

	split_into_words(name, &spl_names);

	for (i = 0; i < spl_names.spl_nwords; i++) {
		if (scope != NULL) {
			for (j = 0; j < spl_scopes.spl_nwords; j++) {
				make_ent_macro_t *me = TO_MACRO(new_make_ent(
				    ENT_MACRO, line));
				me->me_scope = strdup(spl_scopes.spl_words[j]);
				me->me_name = strdup(spl_names.spl_words[i]);
				me->me_value = strdup(value);
				me->me_reset_value = reset_val;

				TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
			}
		} else {
			make_ent_macro_t *me = TO_MACRO(new_make_ent(ENT_MACRO,
			    line));
			me->me_scope = NULL;
			me->me_name = strdup(spl_names.spl_words[i]);
			me->me_value = strdup(value);
			me->me_reset_value = reset_val;

			TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
		}
	}

	free_splits(&spl_names);
	if (scope != NULL)
		free_splits(&spl_scopes);
}

void
add_make_target(make_line_t *line, char *name, char *deps)
{
	splitter_t spl_names, spl_deps;
	int i, j;

	split_into_words(name, &spl_names);
	split_into_words(deps, &spl_deps);

	reset_last_target();

	for (i = 0; i < spl_names.spl_nwords; i++) {
		make_ent_target_t *me = TO_TARGET(new_make_ent(ENT_TARGET,
		    line));

		TAILQ_INIT(&me->me_commands);
		TAILQ_INIT(&me->me_deps);

		me->me_name = strdup(spl_names.spl_words[i]);

		for (j = 0; j < spl_deps.spl_nwords; j++) {
			make_ent_dep_t *dep = TO_DEP(new_make_ent(ENT_DEP,
			    line));
			dep->me_name = strdup(spl_deps.spl_words[j]);
			TAILQ_INSERT_TAIL(&me->me_deps, TO_ENT(dep),
			    me_linkage);
		}

		TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);

		add_last_target(me);
	}

	free_splits(&spl_names);
	free_splits(&spl_deps);
}

void
add_make_include(make_line_t *line, char *path)
{
	make_ent_include_t *me = TO_INCLUDE(new_make_ent(ENT_INCLUDE, line));
	me->me_path = strdup(path);

	TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
}

void
add_make_command(make_line_t *line, char *command)
{
	int i;
	make_ent_command_t *me;
	if (nlast_targets == 0) {
		fprintf(stderr, "This command does not belong to a target?!\n");
		exit(1);
	}

	me = TO_COMMAND(new_make_ent(ENT_COMMAND, line));
	me->me_command = strdup(command);

	for (i = 0; i < nlast_targets; i++)
		TAILQ_INSERT_TAIL(&last_targets[i]->me_commands, TO_ENT(me),
		    me_linkage);
}

/* vim: set ts=8 tw=80 noet sw=8 */
