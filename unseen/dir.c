#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <sys/types.h>
#include <sys/param.h>

/* #include <sys/dir.h> */

/* #include <glob.h> */

#include <dirent.h>

#include <sys/stat.h>
#include <unistd.h>

extern int alphasort();

static char *table = NULL;

int myselect(const struct dirent *entry) {
	
	char *t;
	
	if(
	(strcmp(entry->d_name, ".") == 0) || 
	(strcmp(entry->d_name,"..") == 0)
	) return 0;
	
	t = strrchr(entry->d_name, '.');
	if (t && (strcmp(t, ".data") != 0)) return 0;

	if (table)
		if (strncmp(entry->d_name, table, strlen(table)) != 0)
			return 0;
	return 1;
}

char **ls(char *path, char *tablename, int *n) {
	int count;
	int i;
	struct dirent **dp;
	char **files;
	table = tablename;
	count = scandir(path, &dp, myselect, alphasort);
	if (count <= 0) {
		printf("No files match %s in %s\n", tablename, path);
		return NULL;
	}
	printf("%d files match %s in %s\n", count, table, path);
	*n = count;
	files = (char **) malloc(count * sizeof(char *));
	for (i = 0; i < count; i++) {
		files[i] = malloc(strlen(path) + 
			strlen(dp[i]->d_name) + 1 + 1); // +1 for "/"
		sprintf(files[i], "%s/%s", path, dp[i]->d_name);
		free(dp[i]);
	}
	free(dp);
	return files;
}

/*
 * Performs the shell command:
 *
 * ls -l data-* /<tablename>*.data'

char **pls(char *tablename, int *n) {
	
	char **files;
	int count;

	char pattern[1024];
	glob_t gb;

	int e; // glob error

	int i;
	
	// Set 'ls' pattern.
	sprintf(pattern, "data*/ /* %s*.data", tablename);
	
	// Search for pattern according to shell.
	gb.gl_offs = 2;
	if ((e = glob(pattern, GLOB_DOOFFS, NULL, &gb)) != 0) {
		
		if (e == GLOB_NOSPACE) fprintf(stderr, "GLOB_NOSPACE\n");
		else
		if (e == GLOB_ABORTED) fprintf(stderr, "GLOB_ABORTED\n");
		else
		if (e == GLOB_NOMATCH) fprintf(stderr, "GLOB_NOMATCH\n");

		globfree(&gb);
		return NULL;
	}
	gb.gl_pathv[0] = "ls";
	gb.gl_pathv[1] = "-l";
	
	count = gb.gl_pathc - gb.gl_offs;
	
	files = (char **) malloc(count * sizeof(char *));
	for (i = 0; i < count; i++) {
		files[i] = strdup(gb.gl_pathv[i+2]);
	}
	globfree(&gb);
	*n = count;
	return files;
}

*/
