#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int clock_hand;

int clock_evict() {
	
    int frame = coremap[clock_hand].pte->frame >> PAGE_SHIFT;
    
    if (!(frame & PG_REF)) {
        return clock_hand;
    }
    
    while (frame & PG_REF) {
		// if this frame is set to 1 then we clear that bit and move to the next frame.
		frame = frame & ~PG_REF;
		coremap[clock_hand].pte->frame = frame << PAGE_SHIFT;
		clock_hand = (clock_hand == 0) ? clock_hand - 1 : memsize - 1;
	}
	
	// if we are here that means we got the one with PG_REF clear one.
	// that means we have the clock_hand at the page to be evicted (if it is
	// valid).
	if (coremap[clock_hand].pte->frame & PG_VALID) {
	    return clock_hand;
	} else {
	    clock_hand = (clock_hand == memsize -1) ? 0 : clock_hand + 1;
	    return clock_hand;
	}
}

/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {

	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm.
 */
void clock_init() {
    clock_hand = memsize - 1;
}

