


#ifndef	_JMAKE_H_
#define	_JMAKE_H_

typedef struct make_line {
	char *ml_file;
	int ml_linemin;
	int ml_linemax;

	char *ml_line;

	TAILQ_ENTRY(make_line) ml_linkage;

} make_line_t;

extern int parse_line(make_line_t *);


#endif	/* _JMAKE_H_ */
