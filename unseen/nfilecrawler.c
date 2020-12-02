#define _FILE_OFFSET_BITS 64
#include <stdio.h>

#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <sys/types.h>

#include <fcntl.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "filecrawler.h"

#include "timestamp.h"
#include "rtab.h"

#include "util.h"
#include "list.h"

#include "sqlstmts.h"

#include "y.tab.h"

static void setdropped(Filecrawler *fc, long long num) {

	*(fc->drop + (int) num) = 1;
	return;
}

static void clrdropped(Filecrawler *fc, long long num) {
	
	*(fc->drop + (int) num) = 0;
	return;
}

static int isdropped(Filecrawler *fc, long long num) {

	return (*(fc->drop + (int) num) == 1);
}

Filecrawler *filecrawler_new(char *tablename) {

	Filecrawler *fc;
	Ndx* n;
	
	char indxf[1024];
	int fd;
	
	struct stat sb;
	long long num;
	
	/* map index in memory */
	sprintf(indxf, "%s.indx", tablename);

	if ((fd = open(indxf, O_RDONLY)) == -1) {
		errorf("Failed to open %s\n", indxf);
		return NULL;
	}

	if (fstat(fd, &sb) == -1) {
		errorf("fstat %s failed\n", indxf);
		return NULL;
	}

	if (!S_ISREG(sb.st_mode)) {
		errorf("%s is not a regular file\n", indxf);
		return NULL;
	}
	
	if (sb.st_size == 0) { /* If true, mmap will fail. */
		debugf("%s is empty\n", indxf);
		return NULL;
	}

	if ((n = (Ndx *)mmap(0, sb.st_size, PROT_READ, MAP_SHARED, 
		fd, 0)) == MAP_FAILED) {
		
		errorf("Failed to map %s\n", indxf);
		return NULL;
	}

	if (close(fd) == -1) {
		errorf("Failed to close %s\n", indxf);
		return NULL;
	}

	fc = malloc(sizeof(Filecrawler));
	fc->n = n;
	fc->sb = sb;
	fc->empty = 0; /* false */
	
	debugf("[filecrawler] %lld bytes; %lld structs\n", 
	fc->sb.st_size, fc->sb.st_size / sizeof(Ndx));
	
	num = (long long) (fc->sb.st_size / sizeof(Ndx));

	fc->drop=(int *) malloc(num * sizeof(int));

	if (! (fc->sb.st_size > 0)) {
		debugf("Empty filecrawler\n");
		fc->empty = 1; /* true */
	}

	char *start = timestamp_to_datestring(fc->n[0].timeStamp);
	char *until = timestamp_to_datestring(fc->n[num-1].timeStamp);
	debugf("%s [%s, %s]\n", tablename, start, until);
	free(start);
	free(until);
	return fc;
}

void filecrawler_free(Filecrawler *fc) {

	if (munmap(fc->n, fc->sb.st_size) == -1) {
		errorf("munmap failed\n");
	}

	if (fc->drop)
		free(fc->drop);

	free(fc);
	debugf("filecrawler free'd\n");
}

void filecrawler_project(Filecrawler *fc, T *table, Rtab *results) {

	List *rowlist;

	long long num;
	long long max;

	FILE *fs = NULL;
	
	tstamp_t tstamp;

	char b[1024];
	char *p; /* pointer to `b' */
	char t[1024];
	int size;

	Rrow *o;
	Rrow *r;
	
	int i;

	char *name;
	int indx;
	int length;

	rowlist=list_new();

	max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	for (num = 0; num <= max; num++) {
		
		if (isdropped(fc, num)) {
			debugvf("%lld is dropped\n", num);
			continue;
		}
		
		fs = t_file(table, fc->n[num].timeStamp);
		if (fs == NULL) {
			debugvf("File not found\n");
			continue;
		}
		
		fseeko(fs, fc->n[num].byteOffset, SEEK_SET);
		fgets(b, sizeof(b), fs);
		debugf("%s", b);
		tstamp = fc->n[num].timeStamp;
	
		/* Row (from line) */
		o = malloc(sizeof(Rrow));
		o->cols = malloc(table->n * sizeof(char*));
		p = b;
		p = rtab_fetch_str(p, t, &size); /* skip timestamp */
		for (i = 0; i < table->n; i++) {
			p = rtab_fetch_str(p, t, &size);
			o->cols[i] = strdup(t);
		}

		/* Row selected */
		r = malloc(sizeof(Rrow));
		r->cols = malloc(results->ncols * sizeof(char*));
		
		for (i = 0; i < results->ncols; i++) {
			name = results->colnames[i];
			indx = t_lookup_colindex(table, name);
			if (indx == -1) { /* timestamp */
			
			r->cols[i] = timestamp_to_string(tstamp);
			debugvf("%d: %s\n", i, r->cols[i]);

			} else {
			
			length = strlen(o->cols[indx]) + 1;
			r->cols[i] = malloc(length);
			strcpy(r->cols[i], o->cols[indx]);
			debugvf("%d: %s\n", i, r->cols[i]);
			
			}
		}

		list_append(rowlist, r);
		for (i = 0; i < table->n; i++)
			if (o->cols[i]) free(o->cols[i]);
		free(o->cols);
		free(o);
	}
	t_closefiles(table);
	
	/* Project results */
	results->nrows = list_len(rowlist);
	results->rows = (Rrow**) list_to_array(rowlist);
	list_free(rowlist);
	return;
}

static int compts(int op, tstamp_t ts, tstamp_t then) {
	
	int ans;
	
	switch(op) {
	case LESS: 
		ans = (ts < then); 
		break;
	case LESSEQ: 
		ans = (ts <= then); 
		break;
	case GREATER: 
		ans = (ts > then); 
		break;
	case GREATEREQ: 
		ans = (ts >= then); 
		break;
	default: 
		ans = 0; 
		break;
	}
	
	return ans;
}

static long long gotostart(int op, Filecrawler *fc, tstamp_t then) {

	long long num;
	long long max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	
	if (fc->empty)
		return -1;
	
	if (op == GREATER || op == GREATEREQ) {
		
		for (num = max - 1; num >= 0; num--) {
			if (! compts(op, fc->n[num].timeStamp, then))
				return num;
		}

	} else {
		
		for (num = 0; num < max; num++) {
			if (! compts(op, fc->n[num].timeStamp, then))
				return num;
		}

	}

	return -1;
}

static void filecrawler_tuplewindow(Filecrawler *fc, sqlwindow *win) {
	
	long long num;
	long long max;
	long long min;

	if (win->num < 0) {
		errorf("Invalid row number %d in tuple window\n", 
			win->num);
		return;
	}

	max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	min = max - win->num;
	debugf("[%lld, %lld)\n", min, max);
	
	for (num = min; num < max; num++)
		clrdropped(fc, num);

	return;
}

/*
void filecrawler_timewindow(Filecrawler *fc, sqlwindow *win) {
	return;
}
*/

static void filecrawler_sincewindow(Filecrawler *fc, sqlwindow *win) {
	long long num;
	long long min;
	long long max;
	
	min = gotostart(GREATER, fc, win->tstampv);
	if (min < 0) {
		debugf("[filecrawler] since window empty\n");
		fc->empty = 1;
		return;
	}
	/* drop until min */
	max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	debugf("(%lld, %lld)\n", min, max);
	
	for (num = min + 1; num < max; num++)
		clrdropped(fc, num);

	return;
}

static void filecrawler_intervalwindow(Filecrawler *fc, sqlwindow *win) {
	long long num;
	long long min; 
	long long max;
	
	min = gotostart((win->intv).leftOp, fc, (win->intv).leftTs);
	if (min < 0) {
		debugvf("[filecrawler] interval window empty\n");
		fc->empty = 1;
		return;
	}
	max = gotostart((win->intv).rightOp, fc, (win->intv).rightTs);
	debugf("(%lld, %lld)\n", min, max);
	
	for (num = min + 1; num < max; num++)
		clrdropped(fc, num);
	
	return;
}

void filecrawler_window(Filecrawler *fc, sqlwindow *win) {
	long long num;
	long long max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	for (num = 0; num <= max; num++)
		setdropped(fc, num);

	switch(win->type) {
	
	case SQL_WINTYPE_TPL:
		debugf("[filecrawler: wintype_tpl]\n");
		filecrawler_tuplewindow(fc, win);
		break;
	
	case SQL_WINTYPE_TIME:
		debugf("[filecrawler: wintype_time]\n");
		debugf("Time windows are not supported yet.\n");
		/* filecrawler_timewindow(fc, win); */
		break;
	
	case SQL_WINTYPE_SINCE:
		debugf("[filecrawler: wintype_since]\n");
		filecrawler_sincewindow(fc, win);
		break;
	
	case SQL_WINTYPE_INTERVAL:
		debugf("[filecrawler: wintype_interval]\n");
		filecrawler_intervalwindow(fc, win);
		break;
	
	case SQL_WINTYPE_NONE:
		debugf("[filecrawler: wintype_none]\n");
		filecrawler_reset(fc);
		break;
	
	default:
		errorf("Invalid window type.\n");
		break;
	}

	return;
}

static int compare(int op, void *cVal, int *cType, 
	union filterval *filVal) {
	
	if (cType == PRIMTYPE_INTEGER) {
	
	long long val = strtoll((char *)cVal, NULL, 10);
	
	switch(op) {
	
	case SQL_FILTER_EQUAL:
		return (val == filVal->intv); 
		break;
	case SQL_FILTER_GREATER:
		return (val > filVal->intv); 
		break;
	case SQL_FILTER_LESS:
		return (val < filVal->intv); 
		break;
	case SQL_FILTER_LESSEQ:
		return (val <= filVal->intv); 
		break;
	case SQL_FILTER_GREATEREQ:
		return (val >= filVal->intv); 
		break;
	}

	} else if (cType == PRIMTYPE_REAL) {
	
	double val = strtod((char *)cVal, NULL);
	
	switch(op) {
	
	case SQL_FILTER_EQUAL:
		return (val == filVal->realv); 
		break;
	case SQL_FILTER_GREATER:
		return (val > filVal->realv); 
		break;
	case SQL_FILTER_LESS:
		return (val < filVal->realv); 
		break;
	case SQL_FILTER_LESSEQ:
		return (val <= filVal->realv); 
		break;
	case SQL_FILTER_GREATEREQ:
		return (val >= filVal->realv); 
		break;
	}

	} else if (cType == PRIMTYPE_TIMESTAMP) {
	
	unsigned long long val = *(tstamp_t *)cVal;
	
	switch(op) {
	
	case SQL_FILTER_EQUAL:
		return (val == filVal->tstampv); 
		break;
	case SQL_FILTER_GREATER:
		return (val > filVal->tstampv); 
		break;
	case SQL_FILTER_LESS:
		return (val < filVal->tstampv); 
		break;
	case SQL_FILTER_LESSEQ:
		return (val <= filVal->tstampv); 
		break;
	case SQL_FILTER_GREATEREQ:
		return (val >= filVal->tstampv); 
		break;
	}
	
	}
   	
	return 0; /* false */
}

static int passed_filter(Rrow *o, tstamp_t t, T *table, int nfilters, 
	sqlfilter **filters, int filtertype) {
	int i;
	char *filName;
	union filterval filVal;
	int filSign;
	int colIdx;
	void *colVal;
	int *colType;
	for (i = 0; i < nfilters; i++) {
		filName = filters[i]->varname;
		memcpy(&filVal, &filters[i]->value, 
			sizeof(union filterval));
		filSign = filters[i]->sign;
		colIdx = t_lookup_colindex(table, filName);
		if (colIdx == -1)  { /* i.e. an invalid variable name */
			if (strcmp(filName, "timestamp") == 0) {
				colVal = (void *)&(t);
				colType = PRIMTYPE_TIMESTAMP;
			} else {
				errorf("Invalid column name %s\n", filName);
				return 1; /* pass filter */
			}
		} else {
			colType = table->type[colIdx];
			colVal = (void *)(o->cols[colIdx]);
		}
		if (filtertype == SQL_FILTER_TYPE_OR) {
			debugf("Filtertype is OR\n");
			if (compare(filSign, colVal, colType, &filVal)) {
				return 1;
			}
		} else { /* defaults to AND */
			debugf("Filtertype is AND\n");
			if (! compare(filSign, colVal, colType, &filVal)) {
				return 0;
			}
		}
	}
	/* All filters are passed; or not */
	if (filtertype == SQL_FILTER_TYPE_OR)
		return 0;
	return 1;
}

void filecrawler_filter(Filecrawler *fc, T *table, int nfilters, 
	sqlfilter **filters, int filtertype) {
	
	long long num;
	long long max;
	
	FILE *fs = NULL;

	Rrow *o;
	int i;
	
	tstamp_t tstamp;
	
	char b[1024];
	char *p;
	char t[1024];
	int size;
	
	if (fc->empty) {
		debugvf("Filecrawler empty (doing nothing)\n");
		return;
	}

	max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	for (num = 0; num <= max; num++) {

		if (isdropped(fc, num)) {
			debugvf("%lld is dropped\n", num);
			continue;
		}

		fs = t_file(table, fc->n[num].timeStamp);
		if (! fs) {
			debugvf("File not found\n");
			continue;
		}

		fseeko(fs, fc->n[num].byteOffset, SEEK_SET);
		fgets(b, sizeof(b), fs);
		debugf("%s", b);

		tstamp = fc->n[num].timeStamp;
		
		/* Row (from line) */
		o = malloc(sizeof(Rrow));
		o->cols = malloc(table->n * sizeof(char*));
		p = b;
		p = rtab_fetch_str(p, t, &size); /* skip timestamp */
		for (i = 0; i < table->n; i++) {
			p = rtab_fetch_str(p, t, &size);
			o->cols[i] = strdup(t);
		}
		if (! passed_filter(o, tstamp, table, nfilters, 
			filters, filtertype)) {
			debugvf("Filter not passed.\n");
			setdropped(fc, num);
		}

		for (i = 0; i < table->n; i++)
			if (o->cols[i]) free(o->cols[i]);
		free(o->cols);
		free(o);
	}
	t_closefiles(table);
	return;
}

long long filecrawler_dump (Filecrawler *fc) {
	long long num;
	long long max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	long long cnt = 0;
	char *tmp;
	for (num = 0; num <= max; num++) {
		tmp = timestamp_to_datestring(fc->n[num].timeStamp);
		debugf("%s\t%ld\t%d\n", tmp, (long)fc->n[num].byteOffset,
			*(fc->drop + (int) num));
		free(tmp);
		cnt++;
	}
	debugf("%lld elements indexed\n", cnt);
	return cnt;
}

long long filecrawler_count (Filecrawler *fc) {
	long long num;
	long long max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	long long cnt = 0;
	for (num = 0; num <= max; num++) {
		if (! isdropped(fc, num)) cnt++;
	}
	return cnt;
}

void filecrawler_reset (Filecrawler *fc) {
	long long num;
	long long max = (long long) (fc->sb.st_size / sizeof(Ndx)) - 1;
	for (num = 0; num <= max; num++) {
		clrdropped(fc, num);
	}
	return;
}

