#ifndef _LINKEDLIST_H_
#define _LINKEDLIST_H_

/*
 * interface to generic linked list
 * supports add2head, add2tail; remove is always from head
 * 
 * up to caller to provide thread safety if needed
 *
 * provides an iterator to iterate over contents
 */

typedef struct linkedlist LinkedList;
typedef struct lliterator LLIterator;

/*
 * ll_create - created a new linked list
 */
LinkedList *ll_create(void);

/*
 * ll_length - return number of entries in the list
 */
unsigned long ll_length(LinkedList *ll);

/*
 * ll_add2head - inserts new entry at the head of the list
 *               if malloc error, returns 0; otherwise, returns 1
 */
int ll_add2head(LinkedList *ll, void *datum);

/*
 * ll_add2tail - inserts new entry at the tail of the list
 *               if malloc error, returns 0; otherwise, returns 1
 */
int ll_add2tail(LinkedList *ll, void *datum);

/*
 * ll_remove - removes entry from head or list
 *             returns associated datum or NULL
 */
void *ll_remove(LinkedList *ll);

/*
 * ll_delete - deletes linked list after first freeing up any remaining elements
 */
void ll_delete(LinkedList *ll);

/*
 * ll_iter_create - creates an iterator for running through the linked list
 */
LLIterator *ll_iter_create(LinkedList *ll);

/*
 * ll_iter_next - returns the datum from the next element in the linked list
 *                or NULL if the list is exhausted
 */
void *ll_iter_next(LLIterator *iter);

/*
 * ll_iter_delete - delete the iterator
 */
void ll_iter_delete(LLIterator *iter);

#endif /* _LINKEDLIST_H_ */
