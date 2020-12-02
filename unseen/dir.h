#ifndef _DIR_H_
#define _DIR_H_

char **ls(char *path, char *table, int *n); /* Deprecated. */

char **pls(char *tablename, int *n);

void rls(char *path, char *table, int *n);

#endif /* _DIR_H_ */

