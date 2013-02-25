

#include "jmake.h"

#ifndef	_ENTS_H_
#define	_ENTS_H_

extern void add_make_macro(make_line_t *, char *, char *, char *, boolean_t);
extern void add_make_target(make_line_t *, char *, char *);
extern void add_make_command(make_line_t *, char *);
extern void add_make_include(make_line_t *, char *);

extern void dump_ents();

#endif	/* _ENTS_H_ */
