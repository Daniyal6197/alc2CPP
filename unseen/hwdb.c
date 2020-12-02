/*
 * The Homework Database
 *
 * Authors:
 *    Oliver Sharma and Joe Sventek
 *     {oliver, joe}@dcs.gla.ac.uk
 *
 * (c) 2009. All rights reserved.
 */
#include "hwdb.h"
#include "mb.h"
#include "util.h"
#include "rtab.h"
#include "sqlstmts.h"
#include "parser.h"
#include "indextable.h"
#include "table.h"
#include "hashtable.h"
#include "pubsub.h"
#include "srpc.h"
#include "tslist.h"
#include "automaton.h"
#include "topic.h"
#include "node.h"

#include <stdlib.h>
#include <pthread.h>
#include <string.h>

extern char *progname;
extern void ap_init();

/*
 * static data used by the database
 */
static Indextable *itab;
static Hashtable *querytab;
static int ifUsesRpc = 1;
#ifdef HWDB_PUBLISH_IN_BACKGROUND
static TSList workQ;
static TSList cleanQ;
static pthread_t pubthr[NUM_THREADS];
#endif /* HWDB_PUBLISH_IN_BACKGROUND */

/* Used by sql parser */
sqlstmt stmt;

/*
 * forward declarations for functions in this file
 */
Rtab *hwdb_exec_stmt(int isreadonly);
Rtab *hwdb_select(sqlselect *select);
int hwdb_create(sqlcreate *create);
int hwdb_insert(sqlinsert *insert);
Rtab *hwdb_showtables(void);
int hwdb_save_select(sqlselect *select, char *name);
int hwdb_delete_query(char *name);
Rtab *hwdb_exec_saved_select(char *name);
unsigned long hwdb_subscribe(sqlsubscribe *subscribe);
int hwdb_unsubscribe(sqlunregister *unsub);
int hwdb_register(sqlregister *regist);
int hwdb_unregister(sqlunregister *unregist);
int hwdb_update(sqlupdate *update);
void hwdb_publish(char *tablename);
int hwdb_send_event(Automaton *au, char *buf, int ifdisconnect);
#ifdef HWDB_PUBLISH_IN_BACKGROUND
void *do_publish(void *args);
#endif /* HWDB_PUBLISH_IN_BACKGROUND */

int hwdb_init(int usesRPC) {
    
    progname = "hwdb";
    ifUsesRpc = usesRPC;
    mb_init();
    itab = itab_new();
    querytab = ht_create(HT_QUERYTAB_BUCKETS);
    top_init();			/* initialize the topic system */
    au_init();			/* initialize the automaton system */
#ifdef HWDB_PUBLISH_IN_BACKGROUND
    int i;
    workQ = tsl_create();
    cleanQ = tsl_create();
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_create(&pubthr[i], NULL, do_publish, NULL);
        debugf("Publish thread launched.\n");
    }
#endif /* HWDB_PUBLISH_IN_BACKGROUND */

    return 1;
}

#ifdef HWDB_PUBLISH_IN_BACKGROUND
static void do_cleanup(void) {
    void *b;
    CallBackInfo *cbinfo;
    int size;

    while (tsl_remove_nb(cleanQ, (void *)&cbinfo, &b, &size)) {
        if (cbinfo->type == SUBSCRIPTION) {
            Subscription *sub = ps_id2sub(cbinfo->u.id);
	    if (sub) {
                (void) itab_unsubscribe(itab, cbinfo->u.id, sub);
	    }
	} else {
            au_destroy(au_id(cbinfo->u.au));
	    //printf("cleaning up registered automaton\n");
	}
        mem_free(cbinfo);
    }
}
#endif /* HWDB_PUBLISH_IN_BACKGROUND */

Rtab *hwdb_exec_query(char *query, int isreadonly) {
    void *result;
#ifdef HWDB_PUBLISH_IN_BACKGROUND
    do_cleanup();
#endif /* HWDB_PUBLISH_IN_BACKGROUND */
    result = sql_parse(query);
#ifdef VDEBUG
    sql_print();
#endif /* VDEBUG */
    if (! result)
        return  rtab_new_msg(RTAB_MSG_ERROR, NULL);
    
    return hwdb_exec_stmt(isreadonly);
}

Rtab *hwdb_exec_stmt(int isreadonly) {
    Rtab *results = NULL;
    
    switch (stmt.type) {
        case SQL_TYPE_SELECT:
            results = hwdb_select(&stmt.sql.select);
            if (!results)
                results = rtab_new_msg(RTAB_MSG_SELECT_FAILED, NULL);
            break;
        case SQL_TYPE_CREATE:
            if (isreadonly || !hwdb_create(&stmt.sql.create)) {
                results = rtab_new_msg(RTAB_MSG_CREATE_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
        case SQL_TYPE_INSERT:
            if (isreadonly || !hwdb_insert(&stmt.sql.insert)) {
                results = rtab_new_msg(RTAB_MSG_INSERT_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
		case SQL_TYPE_UPDATE:
            if (isreadonly || !hwdb_update(&stmt.sql.update)) {
                results = rtab_new_msg(RTAB_MSG_UPDATE_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
        case SQL_SHOW_TABLES:
            results = hwdb_showtables();
            break;
        case SQL_TYPE_SAVE_SELECT:
            if (isreadonly || !hwdb_save_select(&stmt.sql.select, stmt.name)) {
                results = rtab_new_msg(RTAB_MSG_SAVE_SELECT_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
        case SQL_TYPE_DELETE_QUERY:
            if (isreadonly || !hwdb_delete_query(stmt.name)) {
                results = rtab_new_msg(RTAB_MSG_DELETE_QUERY_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
        case SQL_TYPE_EXEC_SAVED_SELECT:
            results = hwdb_exec_saved_select(stmt.name);
            if (!results)
                results = rtab_new_msg(RTAB_MSG_EXEC_SAVED_SELECT_FAILED, NULL);
            break;
        case SQL_TYPE_SUBSCRIBE: {
	    unsigned long v;
            if (isreadonly || !(v = hwdb_subscribe(&stmt.sql.subscribe))) {
                results = rtab_new_msg(RTAB_MSG_SUBSCRIBE_FAILED, NULL);
            } else {
                static char buf[20];
		sprintf(buf, "%lu", v);
                results = rtab_new_msg(RTAB_MSG_SUCCESS, buf);
            }
            break;
	 }
        case SQL_TYPE_UNSUBSCRIBE:
            if (isreadonly || !hwdb_unsubscribe(&stmt.sql.unregist)) {
                results = rtab_new_msg(RTAB_MSG_UNSUBSCRIBE_FAILED, NULL);
            } else {
               results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }       
            break;
        case SQL_TYPE_REGISTER: {
            int v;
            if (isreadonly || !(v = hwdb_register(&stmt.sql.regist))) {
                results = rtab_new_msg(RTAB_MSG_REGISTER_FAILED, NULL);
            } else {
                static char buf[20];
		sprintf(buf, "%d", v);
                results = rtab_new_msg(RTAB_MSG_SUCCESS, buf);
            }
            break;
        }
        case SQL_TYPE_UNREGISTER:
            if (isreadonly ||  !hwdb_unregister(&stmt.sql.unregist)) {
                results = rtab_new_msg(RTAB_MSG_UNREGISTER_FAILED, NULL);
            } else {
                results = rtab_new_msg(RTAB_MSG_SUCCESS, NULL);
            }
            break;
        default:
            errorf("Error parsing query\n");
            results = rtab_new_msg(RTAB_MSG_PARSING_FAILED, NULL);
            break;
    }
	reset_statement();
    return results;
}

Rtab *hwdb_select(sqlselect *select) {
    Rtab *results;
    char *tablename;
    
    debugf("HWDB: Executing SELECT:\n");
    
    /* Only 1 table supported for now */
    tablename = select->tables[0];
    
    /* Check table exists */
    if (!itab_table_exists(itab, tablename)) {
        errorf("HWDB: no such table\n");
        return NULL;
    }
    
	/* if a group-by operator is specified, check its validity */
    if (!sqlstmt_valid_groupby(select)) {
        errorf("HWDB: Invalid group-by operator.\n");
        return NULL;
    }

    /* Check column names match */
    if (!itab_colnames_match(itab, tablename, select)) {
        errorf("HWDB: Column names in SELECT don't match with this table.\n");
        return NULL;
    }

    results = itab_build_results(itab, tablename, select);
	
    return results;
}

int hwdb_update(sqlupdate *update) {
	debugf("HWDB: Executing UPDATE:\n");
	/* Check table exists */
	if (!itab_table_exists(itab, update->tablename)) {
		errorf("HWDB: %s no such table\n", update->tablename);
		return 0;
	}
	
	if (itab_update_table(itab, update)) {
		/* hwdb_publish(update->tablename); *//* Notify subscribers */
		return 1;
	}
	return 0;
}

int  hwdb_create(sqlcreate *create) {
    debugf("Executing CREATE:\n");
    
    return itab_create_table(itab, create->tablename, create->ncols,
		create->colname, create->coltype, create->tabletype,
		create->primary_column);
}

static void gen_tuple_string(Table *t, int ncols, char **colvals, char *out) {
    char *p = out;
    char *ts = timestamp_to_string(t->newest->tstamp);
    int i;
    p += sprintf(p, "%s<|>", ts);
    mem_free(ts);
    for (i = 0; i < ncols; i++) {
        p += sprintf(p, "%s<|>", colvals[i]);
    }
}

int  hwdb_insert(sqlinsert *insert) {
    int i;
    Table *tn;
    union Tuple *p;
    char buf[2048];
	Node *n;
    
    debugf("Executing INSERT:\n");
    
    if (! (tn = itab_table_lookup(itab, insert->tablename))) {
        errorf("Insert table name does not exist\n");
        return 0;
    }

    /* Check columns are compatible */
    if (!itab_is_compatible(itab, insert->tablename, 
            insert->ncols, insert->coltype)) {
        errorf("Insert not compatible with table\n");
        return 0;
    }
	
    /* Check values don't violate primary key constraint */
    if ((n = itab_is_constrained(itab, insert->tablename, 
        insert->colval))) {
       	debugf("Transformation %d\n", insert->transform);
    	if (! insert->transform) {
	    errorf("Insert violates primary key\n");
            return 0;
	}
    }
    
    /* allocate space for tuple, copy values into tuple, thread new
     * node to end of table */
    if ( table_persistent(tn) ) {
    	heap_insert_tuple(insert->ncols, insert->colval, tn, n);
    } else {
    	mb_insert_tuple(insert->ncols, insert->colval, tn);
    }
    gen_tuple_string(tn, insert->ncols, insert->colval, buf);
    top_publish(insert->tablename, buf);
    /* Tuple sanity check */
    debugvf("SANITY> tuple key: %s\n",insert->tablename);
    p = (union Tuple *)(tn->newest->tuple);
    for (i=0; i < insert->ncols; i++) {
        debugvf("SANITY> colval[%d] = %s\n", i, p->ptrs[i]);
    }
    
    /* Notify all subscribers */
    hwdb_publish(insert->tablename);

    return 1;
}

Rtab *hwdb_showtables(void) {
    debugf("Executing SHOW TABLES\n");
    return itab_showtables(itab);
}


int  hwdb_save_select(sqlselect *select, char *name) {
    sqlselect *new_select;
    
    debugf("Executing SAVE SELECT\n");
    
    /* Check if name already exists */
    if (ht_lookup(querytab, name) != NULL) {
        warningf("Query save-name already in use: %s\n", name);
        return 0;
    }
    
    /* Copy the select statement */
    new_select = mem_alloc(sizeof(sqlselect));
    new_select->ncols = select->ncols;
    new_select->cols = select->cols;
    new_select->colattrib = select->colattrib;
    new_select->ntables = select->ntables;
    new_select->tables = select->tables;
    new_select->windows = select->windows;
    new_select->nfilters = select->nfilters;
    new_select->filters = select->filters;
    new_select->filtertype = select->filtertype;
    new_select->orderby = select->orderby;
    new_select->isCountStar = select->isCountStar;
    new_select->containsMinMaxAvgSum = select->containsMinMaxAvgSum;
    new_select->groupby_ncols = select->groupby_ncols;
    new_select->groupby_cols = select->groupby_cols;
    select->ncols = 0;	/* all storage inherited by new select */
    select->ntables = 0;
    select->nfilters = 0;
    select->cols = NULL;
    select->colattrib = NULL;
    select->tables = NULL;
    select->windows = NULL;
    select->filters = NULL;
    select->orderby = NULL;
    select->groupby_cols = NULL;
    select->groupby_ncols = 0;
    
    /* Save the query in the query-table */
    ht_insert(querytab, str_dupl(name), new_select);
    
    return 1;
}

int  hwdb_delete_query(char *name) {
    sqlselect *select;
    int i;
    
    debugf("HWDB: Deleting query: %s\n", name);
    
    select = ht_lookup(querytab, name);
    if (!select) {
        warningf("No query stored under the name: %s\n", name);
        return 0;
    }
    
    ht_remove(querytab, name);
    /*
     * free dynamic storage in select, then free select
     */
    if (select->ncols > 0) {
        for (i = 0; i < select->ncols; i++)
            mem_free(select->cols[i]);
        mem_free(select->cols);
        mem_free(select->colattrib);
    }
    if (select->ntables > 0) {
        for (i = 0; i < select->ntables; i++) {
            mem_free(select->tables[i]);
            mem_free(select->windows[i]);
        }
        mem_free(select->tables);
        mem_free(select->windows);
    }
    if (select->nfilters > 0) {
        for (i = 0; i < select->nfilters; i++) {
            mem_free(select->filters[i]->varname);
            mem_free(select->filters[i]);
        }
        mem_free(select->filters);
    }
    if (select->orderby)
        mem_free(select->orderby);
    mem_free(select);
    
    /* Possibly unsubscribe all clients of this query at this point...
     *   -- foreach table
     *      -- foreach subscriber list
     *         -- if queryname matches
     *            -- hwdb_unsubscribe()
     *            NEEDS TO BE DONE ASAP
     */
    
    return 1;
}

Rtab *hwdb_exec_saved_select(char *name) {
    sqlselect *select;
    
    debugf("HWDB: Executing previously saved select: %s\n", name);
    
    select = ht_lookup(querytab, name);
    if (!select) {
        warningf("No query stored under the name: %s\n", name);
        return NULL;
    }
    
    return hwdb_select(select);
    
}

int hwdb_unregister(sqlunregister *unregist) {
    unsigned long id;

    id = strtoul(unregist->id, NULL, 10);
    return au_destroy(id);
}

int hwdb_register(sqlregister *regist) {
    Automaton *au;
    unsigned short port;
    RpcConnection rpc = NULL;
    char buf[10240], *p;
    
    debugf("HWDB: Registering an automaton\n");
    
    /* Open connection to registering process */
    if (ifUsesRpc) {
        port = atoi(regist->port);
        rpc = rpc_connect(regist->ipaddr, port, regist->service, 1l);
        if (! rpc) {
            errorf("Can't connect to callback. Not registering...\n");
            return 0;
        }
    }

    /* compile automaton */
    /* automaton has leading and trailing quotes */
    strcpy(buf, regist->automaton+1);
    p = strchr(buf, '\"');
    *p = '\0';
    au = au_create(buf, rpc);
    if (! au) {
        errorf("Error creating automaton\n");
	rpc_disconnect(rpc);
	return 0;
    }
    
    /* Return the identifier for the automaton */
    
    return au_id(au);
}

unsigned long  hwdb_subscribe(sqlsubscribe *subscribe) {
    sqlselect *select;
    unsigned short port;
    RpcConnection rpc = NULL;
    
    debugf("HWDB: Subscribing to a query\n");
    
    /* Check if query exists */
    select = ht_lookup(querytab, subscribe->queryname);
    if (!select) {
        errorf("Query doesn't exist. Not subscribing...\n");
        return 0;
    }
    
    /* Check query contains valid table */
    if (!itab_table_exists(itab, select->tables[0])) {
        errorf("Query doesn't contain valid table. Not subscribing...\n");
        return 0;
    }
    
    /* Open connection to callback process (a.k.a. subscriber)*/
    port = atoi(subscribe->port);
    rpc = rpc_connect(subscribe->ipaddr, port, subscribe->service, 1l);
    if (! rpc) {
        errorf("Can't connect to callback. Not subscribing...\n");
        return 0;
    }
    
    /* Register to notify callback on table updates */
    return itab_subscribe(itab, select->tables[0], subscribe, rpc);
}

int   hwdb_unsubscribe(sqlunregister *unsub) {
    unsigned long id;
    Subscription *sub;

    debugf("HWDB: unsubscribing %s from table\n", unsub->id); 
    
    id = strtoul(unsub->id, NULL, 10);
    sub = ps_id2sub(id);
    if (! sub)
        return 0;
    ps_delete(id);		/* remove from table */
    
    /* Remove registered callback */
    return itab_unsubscribe(itab, id, sub);

}

Table *hwdb_table_lookup(char *name) {
    return itab_table_lookup(itab, name);
}

void hwdb_publish(char *tablename) {
    List *sublist;
    LNode *node;
    Subscription *sub;
    CallBackInfo *cbinfo;
    Rtab *results;
    
    debugf("HWDB: publishing to subscribers of table: %s\n", tablename);
        
    sublist = itab_get_subscriberlist(itab, tablename);
    
    if (!sublist) {
        debugf("HWDB: No subcribers to publish to.\n");
        return;
    }
    
    
    /* Foreach */
    node = sublist->head;
    while (!list_empty(sublist) && node) {
        
        if (node->val) {
	    	long id = (long)(node->val);
	    	sub = ps_id2sub(id);
			
			if (! sub) {
				node = node->next;
				continue;
			}
            
            /* NB 2: This function could be optimized by caching results
             *       of same query for multiple subscribers.
             */
            results = hwdb_exec_saved_select(sub->queryname);
            
            /* Check if query still exists */
            if (!results) {
                node=node->next;
                continue;
            }
            
#ifdef HWDB_PUBLISH_IN_BACKGROUND
	    cbinfo = (CallBackInfo *)mem_alloc(sizeof(CallBackInfo));
	    if (cbinfo) {
                cbinfo->type = SUBSCRIPTION;
                cbinfo->ifdisconnect = 0;
		cbinfo->u.id = id;
	        tsl_append(workQ, cbinfo, results, 1);
	    } else {
                errorf("Unable to allocate structure for call back\n");
	    }
#else
            /* Send results to subscriber */
            debugf("HWDB: Sending to subscriber: %s:%s:%s\n",
                sub->ipaddr, sub->port, sub->service);
            /* need to check return and unsubscribe if false */
            rtab_send(results, sub->rpc);
            rtab_free(results);
#endif /* HWDB_PUBLISH_IN_BACKGROUND */
        }
        
        node = node->next;
    }
    
}

int hwdb_send_event(Automaton *au, char *buf, int ifdisconnect) {
    int result = 0;	/* assume failure */
#ifdef HWDB_PUBLISH_IN_BACKGROUND
    CallBackInfo *cbinfo = (CallBackInfo *)mem_alloc(sizeof(CallBackInfo));
    if (cbinfo) {
        char *s = str_dupl(buf);
	if (s) {
            cbinfo->type = REGISTRATION;
            cbinfo->ifdisconnect = ifdisconnect;
	    cbinfo->u.au = au;
            tsl_append(workQ, cbinfo, s, 1);
	    result = 1;
        } else {
            errorf("Unable to allocate structure for call back\n");
	    mem_free(cbinfo);
        }
    } else {
        errorf("Unable to allocate structure for call back\n");
    }
#else
    /* Send results to registrant */
    char resp[100];
    unsigned rlen;
    struct qdecl qd;
    int len = strlen(buf) + 1;
    qd.size = len;
    qd.buf = buf;
    result = rpc_call(au->rpc, &qd, len, resp, sizeof(resp), &rlen);	
    if (ifdisconnect)
        rpc_disconnect(au->rpc);
#endif /* HWDB_PUBLISH_IN_BACKGROUND */
    return result;
}

#ifdef HWDB_PUBLISH_IN_BACKGROUND
void *do_publish(void *args) {
    CallBackInfo *ca;
    void *b = args;		/* assignment eliminates unused warning */
    int size;
    int status;
    // long long count = 0;
    
    for (;;) {
        tsl_remove(workQ, (void *)&ca, &b, &size);
	//printf("Removed call back structure %lld from the workQ\n", ++count);
	if (ca->type == SUBSCRIPTION) {
	    	Rtab *results = (Rtab *)b;
	    	Subscription *sub = ps_id2sub(ca->u.id);
		if (sub) {
            	/* Send results to subscriber */
            		debugf("HWDB: Sending to subscriber: %s:%s:%s\n",
                		sub->ipaddr, sub->port, sub->service);
            	/* need to check return and unsubscribe if false */
	    		status = rtab_send(results, sub->rpc);
		} else {
			//fprintf(stderr, "Alex, sub in do_publish() is null.\n");
			status = 0;
		}
		/* Free results anyway. */
		rtab_free(results);
	} else {
            char *buf = (char *)b;
	    	char resp[100];
	    	unsigned rlen;
	    	int len = strlen(buf)+1;
	    	struct qdecl qd;
	    	qd.size = len;
	    	qd.buf = buf;
            status = rpc_call(au_rpc(ca->u.au), &qd, len, resp, sizeof(resp), &rlen);	
	    	mem_free(b);
	}
        if (ca->ifdisconnect)
            rpc_disconnect(au_rpc(ca->u.au));
		if (status)	/* successful, free up CallBackInfo struct */
	    	mem_free(ca);
		else {
            tsl_append(cleanQ, (void *)ca, NULL, 0);  /* free after cleanup */
	    	//printf("Must clean up %s when back in mainline\n", 
			// (ca->type == SUBSCRIPTION) ? "subscription" : "registration");
		}
    }
    return NULL;
}
#endif /* HWDB_PUBLISH_IN_BACKGROUND */
