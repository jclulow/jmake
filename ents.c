

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



typedef TAILQ_HEAD(make_ent_head, make_ent) make_ent_head_t;

make_ent_head_t make_ents = TAILQ_HEAD_INITIALIZER(make_ents);

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

#define	MAX_TARGETS_ON_LINE	8192
static make_ent_target_t *last_targets[MAX_TARGETS_ON_LINE];
static int nlast_targets = 0;

static char *lookup_macro_value(const char *, const char *);
static char *expand_string(make_line_t *, char *);
static char *expand_string_impl(make_line_t *, char *);

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
	fprintf(stderr, "----------------\n%s\t(%s:%d-%d)\n", type,
	    ml->ml_file->mf_path, ml->ml_linemin, ml->ml_linemax);
}

static void
dump_ent(make_ent_t *t, boolean_t expand_values)
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
		    B_TRUE ? "=" : "+=");
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
			if (expand_values == B_TRUE) {
				char *exp = expand_string(m->me_line, TO_COMMAND(e)->me_command);
				fprintf(stderr, "\t\t| %s\n",
				     exp);
				free(exp);
			} else {
				fprintf(stderr, "\t\t| %s\n",
				     TO_COMMAND(e)->me_command);
			}
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
		dump_ent(me, B_FALSE);
	}
}

void
dump_cmd_for_target(char *targ)
{
	make_ent_t *t;
	char *outval = NULL;

	fprintf(stderr, "\n===================== %s =============\n\n", targ);

	TAILQ_FOREACH(t, &make_ents, me_linkage) {
		make_ent_target_t *me;
		if (t->me_type != ENT_TARGET)
			continue;

		me = TO_TARGET(t);
		if (strcmp(targ, me->me_name) != 0)
			continue;

		dump_ent(TO_ENT(me), B_TRUE);
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

#define	MAX_WORDS	1024
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
	char *buf = calloc(1, MAXLINE);
	char *pos, *opos;
	int parens = 0;

	spl->spl_nwords = 0;
	bzero(spl->spl_words, sizeof(spl->spl_words));
	buf[0] = '\0';
	opos = buf;

	for (pos = instr; *pos != '\0'; pos++) {
		char c = *pos;
		char cc = *(pos + 1);

		if (spl->spl_nwords > MAX_WORDS) {
			fprintf(stderr, "too many words!\n");
			exit(1);
		}

		if (c == ')' && --parens > 0) {
			*opos++ = ')';
		} else if (c == '$' && cc == '(') {
			parens++;
			*opos++ = '$';
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

	free(buf);
}

static char *
lookup_macro_value(const char *scope, const char *name)
{
	make_ent_t *t;
	char *outval = NULL;

	if (scope == NULL && (outval = get_from_environ(name)) != NULL) {
		/*
		 * Use override from environment because of -e flag, etc:
		 */
		return (outval);
	}

	TAILQ_FOREACH(t, &make_ents, me_linkage) {
		make_ent_macro_t *me;
		if (t->me_type != ENT_MACRO)
			continue;

		me = TO_MACRO(t);
		if (scope != NULL && strcmp(scope, me->me_scope) != 0)
			continue;

		if (strcmp(name, me->me_name) != 0)
			continue;

		if (outval == NULL || me->me_reset_value == B_TRUE) {
			if (outval == NULL)
				free(outval);
			outval = strdup(me->me_value);
		} else {
			char *x;
			(void) asprintf(&x, "%s %s", outval, me->me_value);
			free(outval);
			outval = x;
		}
	}

	if (outval == NULL)
		return ("");
	else
		return (outval);
}

static char term_seq0[] = {
	':', '%', '=', '%', '\0'
};

static char term_seq1[] = {
	':', '=', '\0'
};

static char *
pattern_replacement(make_line_t *line, const char *name, char *oldpfx,
    char *oldsfx, char *newpfx, char *newsfx)
{
	int i;
	splitter_t spl_val;
	char *tmp;
	char *outstr = NULL;

	tmp = expand_string(line, lookup_macro_value(NULL, name));
	split_into_words(tmp, &spl_val);
	free(tmp);

	for (i = 0; i < spl_val.spl_nwords; i++) {
		char *t;
		char *val = spl_val.spl_words[i];
		int len = strlen(val);
		boolean_t do_pfx = B_FALSE;
		boolean_t do_sfx = B_FALSE;

		if (strlen(oldpfx) == 0) {
			do_pfx = B_TRUE;
		} else if (strstr(val, oldpfx) == val) {
			/*
			 * Check for (and remove) the old prefix.
			 */
			val += strlen(oldpfx);
			len -= strlen(oldpfx);
			do_pfx = B_TRUE;
		}
		if (strlen(oldsfx) == 0) {
			do_sfx = B_TRUE;
		} else if (len >= strlen(oldsfx)) {
			/*
			 * Check for (and remove) the old suffix.
			 */
			char *chkat = val + len - strlen(oldsfx);
#if 0
			fprintf(stderr, "chkat '%s' oldsfx '%s'\n", chkat, oldsfx);
#endif
			if (strcmp(chkat, oldsfx) == 0) {
				*chkat = '\0';
				len -= strlen(oldsfx);
				do_sfx = B_TRUE;
			}
		}

		if (outstr == NULL) {
			asprintf(&outstr, "%s%s%s",
			    do_pfx == B_TRUE ? newpfx : "",
			    val,
			    do_sfx == B_TRUE ? newsfx : "");
		} else {
			asprintf(&t, "%s %s%s%s", outstr,
			    do_pfx == B_TRUE ? newpfx : "",
			    val,
			    do_sfx == B_TRUE ? newsfx : "");
			free(outstr);
			outstr = t;
		}
	}

	free_splits(&spl_val);
	return (outstr);
}

static char *
expand_string_impl(make_line_t *ml, char *expstr)
{
	char *ret = NULL;
	char *tmp;

	tmp = expand_string(ml, expstr);
	if (strchr(tmp, ':') != NULL && strchr(tmp, '%') != NULL) {
		char *work = strdup(tmp);
		char *pos;
		char *terms[5];
		int term = 0;

		bzero(terms, sizeof(terms));

		/*
		 * Pattern Replacement.
		 *
		 * $(SOME_MACRO:oldpfx%oldsfx=newprefix%newsuffix)
		 */
		terms[term] = work;
		for (pos = work; *pos != '\0'; pos++) {
			if (term > 4) {
				fprintf(stderr, "unsupported pattern replacement: %s\n", tmp);
				exit(1);
			}
			if (*pos == term_seq0[term]) {
				*pos = '\0';
				terms[++term] = pos + 1;
			}
		}
#if 0
		fprintf(stderr, "[%s] s/^%s/%s/ s/%s$/%s/\n", terms[0],
		    terms[1], terms[2], terms[3], terms[4]);
#endif
		ret = pattern_replacement(ml, terms[0], terms[1], terms[2],
		    terms[3], terms[4]);
#if 0
		fprintf(stderr, "res: %s\n", ret);
#endif
		free(work);
	} else if (strchr(tmp, ':') != NULL) {
		char *work = strdup(tmp);
		char *pos;
		char *terms[3];
		int term = 0;

		bzero(terms, sizeof(terms));

		/*
		 * Suffix Replacement:
		 *
		 * $(SOME_MACRO:suffix=newsuffix)
		 */
		terms[term] = work;
		for (pos = work; *pos != '\0'; pos++) {
			if (term > 2) {
				fprintf(stderr, "unsupported suffix replacement: %s\n", tmp);
				exit(1);
			}
			if (*pos == term_seq1[term]) {
				*pos = '\0';
				terms[++term] = pos + 1;
			}
		}
#if 0
		fprintf(stderr, "[%s] s/^%s$/%s/\n", terms[0], terms[1],
		    terms[2]);
#endif
		ret = pattern_replacement(ml, terms[0], "", terms[1], "", terms[2]);
#if 0
		fprintf(stderr, "res: %s\n", ret);
#endif
		free(work);
	} else {
		ret = expand_string(ml, lookup_macro_value(NULL, tmp));
	}
	free (tmp);

	return (ret);
}

static char *
expand_string(make_line_t *ml, char *instr)
{
	char *pos;
	char *subbuf = malloc(MAXLINE);
	char *buf = malloc(MAXLINE);
	char *opos = buf;
	char *spos = subbuf;
	int parens = 0;
	char *ret;

	*opos = '\0';

	for (pos = instr; *pos != '\0'; pos++) {
		char c = *pos;
		char cc = *(pos + 1);
#if 0
		fprintf(stderr, "parens %d\n", parens);
#endif

		if (parens > 0) {
#if 0
			fprintf(stderr, "subbuf: %s\n", subbuf);
#endif
			if (c == ')' && --parens == 0) {
				char *subexp = expand_string_impl(ml, subbuf);
				char *xx;

				/*fprintf(stderr, "mid: %s\n", expand_string(subbuf));*/

				for (xx = subexp; *xx != '\0'; xx++)
					*opos++ = *xx;
				*opos = '\0';
				free(subexp);
			} else if (c == '(') {
				parens++;
				*spos++ = c;
				*spos = '\0';
			} else {
				*spos++ = c;
				*spos = '\0';
			}
		} else if (c == '$' && cc == '(') {
			/*
			 * Full Expansion.
			 */
			parens++;
			pos++; /* Skip '(' */
			subbuf[0] = '\0';
			spos = subbuf;
		} else if (c == '$') {
			/*
			 * Single Character Expansion.
			 */
			if (cc == '$') {
				*opos++ = '$';
				*opos = '\0';
				pos++; /* Skip '$' */
			} else if (cc == '<') {
				/*
				 * XXX
				 */
				*opos++ = '$';
				*opos++ = '<';
				*opos = '\0';
				pos++; /* Skip '<' */
			} else {
				if (ml != NULL)
					fprintf(stderr, "[%s:%d-%d] ",
					    ml->ml_file->mf_path,
					    ml->ml_linemin, ml->ml_linemax);
				fprintf(stderr, "need expansion for $%c\n", cc);
				if (ml != NULL)
					fprintf(stderr, "%s\n", ml->ml_line);
				exit(5);
			}
		} else {
			*opos++ = c;
			*opos = '\0';
		}
	}

	if (parens > 0) {
		fprintf(stderr, "unbalanced parens in string: %s\n", instr);
		exit(6);
	}

#if 0
	fprintf(stderr, "EXPAND STRING: %s\n", instr);
	fprintf(stderr, "           TO: %s\n", buf);
#endif

	ret = strdup(buf);

	free(subbuf);
	free(buf);

	return (ret);
}

void
dump_macro(char *scope, char *targ)
{
	make_ent_t *t;
	char *outval = NULL;
	char *tmp;

	asprintf(&tmp, "$(%s)", targ);

	fprintf(stderr, "\n===================== %s =============\n\n", targ);

	fprintf(stderr, "raw:\n\t%s\n", lookup_macro_value(scope, targ));

	fprintf(stderr, "\n\nexp:\n\t%s\n\n", expand_string(NULL, tmp));

	free(tmp);
}

void
add_make_macro(make_line_t *line, char *scope, char *name, char *value, boolean_t reset_val)
{
	char *exp_name = expand_string(line, name);
	splitter_t spl_scopes, spl_names;
	int i, j;

	if (scope != NULL) {
		char *exp_scope = expand_string(line, scope);
		split_into_words(exp_scope, &spl_scopes);
		free(exp_scope);
	}

	split_into_words(exp_name, &spl_names);
	free(exp_name);

	for (i = 0; i < spl_names.spl_nwords; i++) {
		if (scope != NULL) {
			for (j = 0; j < spl_scopes.spl_nwords; j++) {
				make_ent_macro_t *me = TO_MACRO(new_make_ent(
				    ENT_MACRO, line));
				me->me_scope = strdup(spl_scopes.spl_words[j]);
				me->me_name = strdup(spl_names.spl_words[i]);
				me->me_value = trim(value);
				me->me_reset_value = reset_val;

				TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
			}
		} else {
			make_ent_macro_t *me = TO_MACRO(new_make_ent(ENT_MACRO,
			    line));
			me->me_scope = NULL;
			me->me_name = strdup(spl_names.spl_words[i]);
			me->me_value = trim(value);
			me->me_reset_value = reset_val;

			TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);
		}
	}

	free_splits(&spl_names);
	if (scope != NULL)
		free_splits(&spl_scopes);
}

static char *unsupported_targets[] = {
	".DEFAULT",
	".DONE",
	".FAILED",
	".GET_POSIX",
	".IGNORE",
	".INIT",
	".KEEP_STATE",
	".KEEP_STATE_FILE",
	".MAKE_VERSION",
	".NO_PARALLEL",
	".PARALLEL",
	".POSIX",
	".PRECIOUS",
	".SCCS_GET",
	".SCCS_GET_POSIX",
	".SILENT",
	".SUFFIXES",
	".WAIT",
	NULL
};

static boolean_t
is_unsupported_target(const char *unsup)
{
	int i;
	for (i = 0; unsupported_targets[i] != NULL; i++) {
		if (strcmp(unsup, unsupported_targets[i]) == 0)
			return (B_TRUE);
	}
	return (B_FALSE);
}

void
add_make_target(make_line_t *line, char *name, char *deps)
{
	char *exp_name = expand_string(line, name);
	char *exp_deps = expand_string(line, deps);
	splitter_t spl_names, spl_deps;
	int i, j;

	split_into_words(exp_name, &spl_names);
	split_into_words(exp_deps, &spl_deps);
	free(exp_name);
	free(exp_deps);

	reset_last_target();

	for (i = 0; i < spl_names.spl_nwords; i++) {
		make_ent_target_t *me = TO_TARGET(new_make_ent(ENT_TARGET,
		    line));

		TAILQ_INIT(&me->me_commands);
		TAILQ_INIT(&me->me_deps);

		me->me_name = strdup(spl_names.spl_words[i]);
		if (is_unsupported_target(me->me_name)) {
			fprintf(stderr, "WARNING: [%s:%d] unsupported target: "
			    "%s\n", me->me_line->ml_file->mf_path,
			    me->me_line->ml_linemin, me->me_name);
		}

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
add_make_include(make_line_t *line, char *path, char **include_file)
{
	make_ent_include_t *me = TO_INCLUDE(new_make_ent(ENT_INCLUDE, line));
	me->me_path = expand_string(line, path);

	TAILQ_INSERT_TAIL(&make_ents, TO_ENT(me), me_linkage);

	if (include_file != NULL)
		*include_file = strdup(me->me_path);
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
