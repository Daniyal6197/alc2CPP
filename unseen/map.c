#define _FILE_OFFSET_BITS 64
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>

#include <pthread.h>

#include "map.h"
#include "dir.h"

#include "util.h"

#include "filecrawler.h"

int f_index(F *f, char *in, char *out) { /* path embedded in filename(s) */
	
	Ndx n;
	
	char b[1024], t[1024];
	int size;

	unsigned long count;

	FILE *fin, *fout;
	
	/*
	char cwd[1024];

	if (! getcwd(cwd, 1024)) {
		errorf("Failed to get cwd (max char. 1024)\n");
		return 1;
	}
	debugf("cwd %s\n", cwd);

	if (chdir(path) != 0) {
		errorf("Failed to cd %s\n", path);
		return 1;
	}
	*/

	if (access(out, R_OK) == 0) {
		debugf("%s already indexed\n", in);
		f->indexed = 1;
		return 1;
	}

	fin = fopen(in,  "r");
	if (! fin) {
		errorf("Failed to open %s\n", in);
		return 1;
	}
	fout = fopen(out, "w");
	if (! fout) {
		errorf("Failed to open %s\n", out);
		fclose(fin); /* close input */
		return 1;
	}

	debugf("sizeof(Ndx) = %d\n", sizeof(Ndx));
	debugf("sizeof(off_t) = %d\n", sizeof(off_t));
	
	char *tmp;
	count = 0;

	while (! feof(fin)) {
		
		memset(b, 0, sizeof(b));
		memset(t, 0, sizeof(t));

		n.byteOffset = ftello(fin);
		(void) fgets(b, sizeof(b), fin);
		if (b[0] == '\0') {
			debugvf("[empty line; skip]\n");
			continue;
		}
		(void) rtab_fetch_str(b, t, &size);
		n.timeStamp = string_to_timestamp(t);
		tmp = timestamp_to_datestring(n.timeStamp);
		/* debugf("%s (%s)\t%ld\n", t, tmp, (long)n.byteOffset); */
		free(tmp);
		fwrite(&n, sizeof(Ndx), 1, fout);
		count++;
	}
	fflush(fout);
	debugf("%lu lines processed\n", count);
	fclose(fout);
	fclose(fin);
	/*
	chdir(cwd);
	*/
	f->indexed = 1;
	return 0;
}

F *f_init(char *filename) {
	F *f = NULL;
	Filecrawler *fc;
	long long cnt;
	char *t;
	char *index;
	size_t size;
	f = malloc(sizeof(F));
	f->name = malloc(strlen(filename) + 1);
	strcpy(f->name, filename);
	f->oldest = 0LL;
	f->newest = 0LL;
	f->fs = NULL;
	index = malloc(strlen(filename) + 1);
	t = strrchr(f->name, '.');
	size = (t - f->name + 1); /* pointer arithmetic */
	snprintf(index, size, "%s", f->name);
	strcat(index, ".indx");
	debugf("%s ? %s\n", f->name, index);
	if (f_index(f, filename, index) == 0) {
		debugf("%s > %s\n", f->name, index);
	}
	snprintf(index, size, "%s", f->name);
	fc = filecrawler_new(index);
	if (fc) {
		filecrawler_reset(fc);
		cnt = filecrawler_count(fc);
		f->oldest = fc->n[0].timeStamp;
		f->newest = fc->n[cnt-1].timeStamp;
		filecrawler_free(fc);
	}
	free(index);
	return f;
}

static int t_create_index(char *tablename) {
	char indxf[1024];
	FILE *f;
	sprintf(indxf, "%s.indx", tablename);
	f = fopen(indxf, "w");
	if (! f) {
		errorf("Failed to open %s\n", indxf);
		return 0;
	}
	fclose(f);
	return 1;
}

static void t_update_index(char *tablename, F *f) {
	
	char master[1024];

	char *index;
	char *t;
	int size;
	
	FILE *fout;
	
	long long num; 
	long long cnt;
	/* char *tmp; */

	Filecrawler *fc;

	index = malloc(strlen(f->name) + 1);
	t = strrchr(f->name, '.');
	size = (t - f->name + 1); /* pointer arithmetic */
	snprintf(index, size, "%s", f->name);
	
	sprintf(master, "%s.indx", tablename);

	fout = fopen(master, "a");
	if (! fout) {
		debugf("Failed to open %s\n", master);
		free(index);
		return;
	}
	
	fc = filecrawler_new(index);
	if (! fc) {
		free(index);
		fclose(fout);
		return;
	}
	filecrawler_reset(fc);
	cnt = filecrawler_count(fc);
	for (num = 0; num < cnt; num++) {
		/* tmp = timestamp_to_datestring(fc->n[num].timeStamp);
		debugf("%s\t%ld\n", tmp, (long)fc->n[num].byteOffset);
		free(tmp); */
		fwrite(&fc->n[num], sizeof(Ndx), 1, fout);
	}
	debugf("%lld elements written in %s\n", cnt, master);
	filecrawler_free(fc);
	fclose(fout);
	free(index);
	return;
}

void t_index(T *t, char *tablename) {
	int i;
	int size = list_len(t->files);
	F **f = (F**) list_to_array(t->files);
	if (! t_create_index(tablename)) {
		errorf("Failed to create master index for %s\n",
			tablename);
		return;
	}
	for (i = 0; i < size; i++) {
		if (f[i]->indexed)
			t_update_index(tablename, f[i]);
	}
	free(f);
	return;
}

void t_extract_relevant_columns(T *t, sqlselect *select, 
	Rtab *results) {
	int i;
	int isstar;
	int ncols;
	char *p;
	isstar = (select->ncols == 1 && 
		strcmp(select->cols[0], "*") == 0);
	if (isstar) 
		ncols = t->n + 1; /* +1 for timestamp */
	else 
		ncols = select->ncols;
	results->colnames = 
		(char **) malloc(ncols * sizeof(char *));
	results->ncols = ncols;
	for (i = 0; i < ncols; i++) {
		if (isstar) {
			if (i == 0) p = "timestamp";
			else p = t->name[i-1];
		} else p = select->cols[i];
		results->colnames[i] = strdup(p);
	}
	return;
}

static int *t_lookup_coltype(T *t, char *colname) {
	int i;
	
	for (i = 0; i < t->n; i++) {
		if (strcmp(t->name[i], colname) == 0) {
			return t->type[i];
		}
	}
	
	if (strcmp(colname, "timestamp") == 0)
		return PRIMTYPE_TIMESTAMP;

	return NULL;
}

void t_extract_relevant_types(T *t, Rtab *results) {
	int i;
	if (results->ncols > 0) {
		results->coltypes = 
			malloc(results->ncols * sizeof(int*));
		for (i = 0; i < results->ncols; i++) {
		results->coltypes[i] = 
			t_lookup_coltype(t, results->colnames[i]);
		}
	}
	return;
}

int t_lookup_colindex(T *t, char *name) {
	int i;
	for (i = 0; i < t->n; i++) {
		if (strcmp(t->name[i], name) == 0) {
			return i; /* found */
		}
	}
	return -1; /* not found */
}

FILE *t_file(T *t, tstamp_t tstamp) {
	int i;
	F *r = NULL;
	int size = list_len(t->files);
	F **f = (F**) list_to_array(t->files);
	
	for (i = 0; i < size; i++) {
		debugf("%s: %lld [%lld, %lld]\n",
		f[i]->name, tstamp, f[i]->oldest, f[i]->newest);
		if (
		(tstamp >= f[i]->oldest) &&
		(tstamp <= f[i]->newest)
		) {
		debugf("%lld within %s\n", tstamp, f[i]->name);
		r = f[i];
		break;
		}
	}
	free(f);
	if (! r) return NULL;
	if (! r->fs) {
		debugf("Opening %s\n", r->name);
		if ((r->fs = fopen(r->name, "r")) == NULL) {
			errorf("Unable to open %s\n", r->name);
			return NULL;
		}
	}
	return r->fs;
}

void t_closefiles(T *t) {
	int i;
	int size = list_len(t->files);
	F **f = (F**) list_to_array(t->files);
	for (i = 0; i < size; i++) {
		debugf("Check %s\n", f[i]->name);
		if (f[i]->fs != NULL) {
			debugf("Closing %s\n", f[i]->name);
			if (fclose(f[i]->fs) != 0) {
				errorf("Failed to close %s\n", f[i]->name);
			}
			f[i]->fs = NULL;
		}
	}
	free(f);
}

Map *map_new(char *directory) {
	Map *m = NULL;
	m = malloc(sizeof(Map));
	m->ht = ht_create(100);
	m->m_mutex = malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(m->m_mutex, NULL);
	m->directory = strdup(directory);
	debugf("[map_new]\n");
	return m;
}

void map_destroy(Map *map) {
	T *p;
	char **key;
	int n;
	int i, j;
	F *f;
	key = ht_keylist(map->ht, &n);
	debugf("%d table(s) mapped\n", n);	
	for (i = 0; i < n; i++) {
		debugf("key[%d] %s\n", i, key[i]);
		p = ht_lookup(map->ht, key[i]);
		if (p) {
			for (j = 0; j < p->n; j++) free(p->name[j]);
			free(p->name);
			free(p->type);
			while((f = list_remove_first(p->files)) != NULL) {
				free(f->name);
				free(f);
			}
			list_free(p->files);
			free(p);
		}
		ht_remove(map->ht, key[i]);
	}
	free(key);
	ht_free(map->ht);
	free(map->m_mutex);
	free(map->directory);
	free(map);
	return;
}

int map_create_table(Map *map, char *tablename, int ncols, 
	char **colnames, int **coltypes) {
	T *p;
	int i;
	char **f;
	int n;
	pthread_mutex_lock(map->m_mutex);
	p = ht_lookup(map->ht, tablename);
	if (NULL == p) {
		p = malloc(sizeof(T));
		p->n = ncols;
		p->name = (char **) malloc(p->n * sizeof(char *));
		p->type = (int  **) malloc(p->n * sizeof(int  *));
		for (i = 0; i < p->n; i++) {
			p->name[i] = strdup(colnames[i]);
			p->type[i] = coltypes[i];
		}
		p->files = list_new();
		/* populate list with files */
		n = 0;
		f = ls(map->directory, tablename, &n);
		debugf("%d files returned (@%p)\n", n, f);
		if (f) {
			for (i = 0; i < n; i++) {
				F *file = f_init(f[i]);
				list_append(p->files, (void *)file);
				free(f[i]);
			}
			free(f);
			t_index(p, tablename);
		}
		pthread_mutex_init(&p->t_mutex, NULL);
		ht_insert(map->ht, tablename, p);
	} else {
		debugf("Table %s exists. Doing nothing.\n", tablename);
		pthread_mutex_unlock(map->m_mutex);
		return 0;
	}
	pthread_mutex_unlock(map->m_mutex);
	return 1;
}

int map_load(Map *map, char *cmd) {
	
	char table[512];
	char *p;
	
	T *t;
	F *f;
	
	memset(table, 0, sizeof(table));
	
	p = strchr(cmd, '|');
	if (! p) {
		fprintf(stderr, "No '|' in %s.\n", cmd);
		return 0;
	}
	strncpy(table, cmd, strlen(cmd) - strlen(p));
	p++;
	
	pthread_mutex_lock(map->m_mutex);
	
	t = ht_lookup(map->ht, table);
	if (! t) {
		pthread_mutex_unlock(map->m_mutex);
		return 0;
	}
	
	f = f_init(p);
	if (! f) {
		pthread_mutex_unlock(map->m_mutex);
		return 0;
	}

	list_append(t->files, (void *) f);
	t_update_index(table, f);
	pthread_mutex_unlock(map->m_mutex);

	return 1;
}

Rtab *map_build_results(Map *map, char *tablename, 
	sqlselect *select) {

	T *p;
	Rtab *results;
	Filecrawler *fc;

	p = ht_lookup(map->ht, tablename);
	if (NULL == p) {
		errorf("Table %s not found\n", tablename);
		return NULL;
	}

	results = rtab_new();

	t_extract_relevant_columns(p, select, results);
	t_extract_relevant_types(p, results);
	
	fc = filecrawler_new(tablename);
	if (! fc) {
		return results;
	}
	filecrawler_reset(fc);
	filecrawler_dump(fc);
	
	filecrawler_window(fc, select->windows[0]);
	
	filecrawler_filter(fc, p, select->nfilters, select->filters, 
		select->filtertype);
	
	filecrawler_project(fc, p, results);
	
	/* Count, group, or order Rtab results */
	if (select->groupby_ncols > 0) {
		rtab_groupby(results, 
			select->groupby_ncols, 
			select->groupby_cols, 
			select->isCountStar, 
			select->containsMinMaxAvgSum, 
			select->colattrib);
	} else {
		if (select->isCountStar) {
			rtab_countstar(results);
		} else if (select->containsMinMaxAvgSum) {
			rtab_processMinMaxAvgSum(results, 
				select->colattrib);
		}
	}
	rtab_orderby(results, select->orderby);

	filecrawler_reset(fc);
	filecrawler_free(fc);

	return results;
}

