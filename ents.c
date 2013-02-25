

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
	ENT_INCLUDE
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
	char *me_deps;

	make_ent_head_t me_commands;
} make_ent_target_t;

#define	TO_ENT(x)		((make_ent_t *) (x))
#define	TO_MACRO(x)		((make_ent_macro_t *) (x))
#define	TO_TARGET(x)		((make_ent_target_t *) (x))
#define	TO_COMMAND(x)		((make_ent_command_t *) (x))
#define	TO_INCLUDE(x)		((make_ent_include_t *) (x))

make_ent_target_t *last_target = NULL;

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
		fprintf(stderr, "\tname:%s\n\tdeps:%s\n", m->me_name,
		    m->me_deps);
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
	default:
		fprintf(stderr, "Unknown make_ent type: %d\n", type);
		exit(1);
	}
	me = calloc(1, size);
	me->me_type = type;
	me->me_line = line;
	return (me);
}

void
add_make_macro(make_line_t *line, char *scope, char *name, char *value, boolean_t reset_val)
{
	make_ent_macro_t *me = TO_MACRO(new_make_ent(ENT_MACRO, line));

	me->me_scope = scope != NULL ? strdup(scope) : NULL;

	me->me_name = strdup(name);
	me->me_value = strdup(value);
	me->me_reset_value = reset_val;

	TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
}

void
add_make_target(make_line_t *line, char *name, char *deps)
{
	make_ent_target_t *me = TO_TARGET(new_make_ent(ENT_TARGET, line));
	me->me_name = strdup(name);
	me->me_deps = strdup(deps);
	TAILQ_INIT(&me->me_commands);

	TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);

	last_target = me;
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
	make_ent_command_t *me;
	if (last_target == NULL) {
		fprintf(stderr, "This command does not belong to a target?!\n");
		exit(1);
	}

	me = TO_COMMAND(new_make_ent(ENT_COMMAND, line));
	me->me_command = strdup(command);

	TAILQ_INSERT_TAIL(&last_target->me_commands, TO_ENT(me), me_linkage);
}

/* vim: set ts=8 tw=80 noet sw=8 */
