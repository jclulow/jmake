


#ifndef	_JMAKE_H_
#define	_JMAKE_H_

#define	MAXLINE	(128 * 1024)

typedef struct make_file make_file_t;

typedef struct make_line {
	make_file_t *ml_file;
	int ml_linemin;
	int ml_linemax;

	char *ml_line;

	TAILQ_ENTRY(make_line) ml_linkage;

} make_line_t;

typedef TAILQ_HEAD(make_line_head, make_line) make_line_head_t;

typedef struct make_file {
	char *mf_path;

	make_line_head_t mf_lines;
} make_file_t;

extern int parse_line(make_line_t *, char **);
extern char *get_from_environ(const char *);

extern int start(char *);

extern char *trim(const char *);

#endif	/* _JMAKE_H_ */
