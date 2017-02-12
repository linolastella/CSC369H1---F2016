#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int lru_evict() {
	int max_ref = 0;
	int idx, i;
	for(i=0; i<memsize; i++){
		if(max_ref <= coremap[i].count_ref){
			max_ref = coremap[i].count_ref;
			idx = i;
		}
	}
	return idx;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {
	int i;
	for (i=0; i< memsize; i++){
		if(coremap[i].pte == p ){
			// this is the most recent referenced page frame.
			coremap[i].count_ref = 0; //set to 0
		} else {
			// if it's not the one we called for update then increment its count_ref 
			// because it's not the most recent one.
			coremap[i].count_ref++;
		}
	}

	return;
}


/* Initialize any data structures needed for this
 * replacement algorithm
 */
void lru_init() {
}
