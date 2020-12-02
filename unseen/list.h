#ifndef _LIST_H_
#define _LIST_H_

typedef struct lnode {
   struct lnode *next;
   void *val;
} LNode;

typedef struct list {
   LNode *head;
   LNode *tail;
   int len;
} List;

List *list_new();
void list_free(List *ll);
int  list_empty(List *ll);
int  list_len(List *ll);
void list_append(List *ll, void *val);
void *list_remove_first(List *ll);
void **list_to_array(List *ll);

#endif /* _LIST_H_ */
