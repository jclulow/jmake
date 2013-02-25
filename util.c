

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



char *
trim(const char *instr)
{
	const char *pos;
	const char *lhs = NULL, *rhs = NULL;
	char *ret;

	/*
	 * Find left-most non-whitespace character.
	 */
	for (pos = instr; *pos != '\0'; pos++) {
		if (!ISSPACE(*pos)) {
			lhs = pos;
			break;
		}
	}

	if (lhs == NULL)
		return (strdup(""));

	/*
	 * Find right-most non-whitespace character.
	 */
	for (pos = lhs; *pos != '\0'; pos++) {
		if (!ISSPACE(*pos))
			rhs = pos;
	}

	ret = malloc(rhs - lhs + 2);
	bcopy(lhs, ret, rhs - lhs + 1);
	ret[rhs - lhs + 1] = '\0';

	return (ret);
}

/* vim: set ts=8 tw=80 noet sw=8 */
