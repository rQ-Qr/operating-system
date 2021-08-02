/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * Organization method for free blocks : segregated free list (implicit free list, explicit free list)
 * Search method for free blocks: best fit (first fit, next fit)
 * Separation method for blocks : keep small free blocks at the front and large free blocks at the end
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"


/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE	4
#define DSIZE	8
#define CHUNKSIZE	(1<<12)

#define MAX(x, y) ((x) > (y)? (x) : (y)) 

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p)	(*(unsigned int *)(p))
#define PUT(p, val)	(*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp)	((char *)(bp) - WSIZE)
#define FTRP(bp)	((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)	((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)	((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define NEXT_FREE(bp)	((char *)GET(bp))
#define PREV_FREE(bp)	((char *)GET((char*)(bp) + WSIZE))

static char *heap_listp;
static unsigned int *free_list;

static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void cut(void *bp);
static void connect(void *bp);
static void *coalesce(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{	//apply a new space of 32 words
    if((heap_listp = mem_sbrk(16*DSIZE)) == (void *)-1)
    	return -1;
    
    PUT(heap_listp, 0); //alignment padding
    //the first size class contains the free blocks with size from 16-32
    int cnt = 5;
    //allocate space for separated linked list
    free_list = (unsigned int*)(heap_listp + WSIZE);
    //set pointers in separated linked list as null
	while(cnt<=32) {
		PUT(heap_listp+(cnt-4)*WSIZE, 0);
		cnt++;
	} 

    PUT(heap_listp + (29*WSIZE), PACK(DSIZE, 1)); //prologue header
    PUT(heap_listp + (30*WSIZE), PACK(DSIZE, 1)); //prologue footer
    PUT(heap_listp + (31*WSIZE), PACK(0, 1));     //epilogue header
    heap_listp += (15*DSIZE);
    //extend the empty heap with a free block of CHUNKSIZE bytes
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
    	return -1;
	else return 0;
}

// extend_heap - apply the new space from the memory
static void *extend_heap(size_t words) {
	char *bp;
	size_t size;
	// always allocate even number of words to maintain alignment
	size = (words % 2)? (words+1) * WSIZE : words * WSIZE;
	if((long)(bp = mem_sbrk(size)) == -1)
		return NULL;
	//Initialize free block header/footer and the epilogue header
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
	//coalesce if the previous block was free
	return coalesce(bp);
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{	
	size_t asize; 			//adjusted block size
	size_t extendsize;		//amount to extend heap if no fit
	char *bp;
	//ignore spurious requests
	if(size==0) 
		return NULL;
	//adjust block size to include overhead and alignment reqs.
	if(size <= DSIZE)
		asize = 2*DSIZE;
	//round up
	else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
	//search the free list for a fit
	if((bp = find_fit(asize)) != NULL) {
		bp = place(bp, asize);
		return bp;
	} 
	//no fit found. get more memory and place the block
	extendsize = MAX(asize, CHUNKSIZE);
	if((bp = extend_heap(extendsize/WSIZE)) == NULL)
		return NULL;
	bp = place(bp, asize);
	return bp;
}
// find fit free block
static void *find_fit(size_t asize) {
	int cnt = 0;
	size_t size = asize;
	//find the smallest fit size class
	while(size!=0) {
		size=size>>1;
		cnt++;
	} 
	//since the smallest size of block is 16
	//the minimum value of cnt is 5
	while(cnt<=32) {
		if(free_list[cnt-5]==0) {
			cnt++; continue;
		}
		else {
			//initially the pointer points to the head
			char *p = (char *)free_list[cnt-5];
			//find smallest fit free block
			while(p!=0 && GET_SIZE(HDRP(p))<asize) {
				p = NEXT_FREE(p);
			} 
			//no fit found
			if(p==0) {
				cnt++; continue;
			}
			//return free block
			else {
				return (void *)p;
			}
		}
		
	}
	
	return NULL;
}

// get the requested block and generate new free block
static void *place(void *bp, size_t asize) {
	size_t size = GET_SIZE(HDRP(bp));
	cut(bp); //take out bp from the linked list
	
	// if the size of spare block <16, keep it as internal fragmentation
	if(size-asize<16) {
		PUT(HDRP(bp), PACK(size, 1));
		PUT(FTRP(bp), PACK(size, 1));
	}
	// keep the small free block at the front of the memory
	else if(asize>=96){
		PUT(FTRP(bp), PACK(asize, 1));
		PUT((char *)(bp)+size-asize-WSIZE, PACK(asize, 1));
		PUT(HDRP(bp), PACK(size-asize, 0));
		PUT(FTRP(bp), PACK(size-asize, 0));
		char *p = bp;
		connect(p);
		bp = (char *)(bp)+size-asize;
	}
	// keep the large free block at the end of the memory
	else {
		PUT(FTRP(bp), PACK(size-asize, 0));
		PUT((char *)(bp)+asize-WSIZE, PACK(size-asize, 0));
		PUT(HDRP(bp), PACK(asize, 1));
		PUT(FTRP(bp), PACK(asize, 1));
		char *p = (char *)(bp)+asize;
		connect(p);
	}
	return bp;	
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
	size_t size = GET_SIZE(HDRP(bp));
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));
	coalesce(bp);
}
// coalesce free blocks
static void *coalesce(void *bp) {
	size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
	size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
	size_t size = GET_SIZE(HDRP(bp));
	// next block is free block
	if(prev_alloc && !next_alloc) {
		size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
		cut(NEXT_BLKP(bp));
		PUT(HDRP(bp), PACK(size, 0));
		PUT(FTRP(bp), PACK(size, 0));
	}
	// prev block is free block
	else if(!prev_alloc && next_alloc) {
		size += GET_SIZE(HDRP(PREV_BLKP(bp)));
		cut(PREV_BLKP(bp));
		PUT(FTRP(bp), PACK(size, 0));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp); 
	}
	// prev and next blocks are free blocks
	else if(!prev_alloc && !next_alloc) {
		size += (GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))));
		cut(PREV_BLKP(bp));
		cut(NEXT_BLKP(bp));
		PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
		PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
		bp = PREV_BLKP(bp);
	}
	connect(bp); // insert the new free block into segregated free list
	
	return bp;
}

// take out the block from the linked list and cut off the connection with other free blocks
static void cut(void *bp) {
	char *next;
	char *prev;
	// 'prev' pointer of next free block points to prev free block
	if((next = NEXT_FREE(bp))!=NULL) {
		PUT(next+WSIZE, *(unsigned int *)((char *)bp+WSIZE));
	}
	// 'next' pointer of prev free block points to next free block
	if((prev = PREV_FREE(bp))!=NULL) {
		PUT(prev-WSIZE, *(unsigned int *)bp);
	}
	// bp is the first element in the array of segregated free lists 
	// array pointer points to next free block
	else {
		size_t size = GET_SIZE(HDRP(bp));
		int cnt = 0;
		while(size!=0) {
			size=size>>1;
			cnt++;
		}
		free_list[cnt-5] = *(unsigned int *)(bp);
	}
}

// insert a new free block into segregated free list
static void connect(void *bp) {
	int cnt = 0;
	size_t size = GET_SIZE(HDRP(bp));
	size_t nsize = size;
	// get the index according to the size of block
	while(nsize!=0) {
		nsize=nsize>>1;
		cnt++;
	}
	// insert the block in ascending order of sizes
	if(free_list[cnt-5]==0 || size < GET_SIZE(HDRP((void *)free_list[cnt-5]))) {
		if(free_list[cnt-5]!=0) {
			PUT((char *)(free_list[cnt-5]) + WSIZE, (unsigned int)((char *)(bp)+WSIZE));
		}	
		PUT((char *)(bp)+WSIZE, 0);
		PUT((char *)bp, free_list[cnt-5]);
		free_list[cnt-5] = (unsigned int)bp;
	}
	else {
		char *cur = (char*)free_list[cnt-5];
		while(GET(cur)!=0 && GET_SIZE(HDRP((void *)cur))<size) {
			cur = (char *)GET(cur);
		}
		if(GET(cur)==0) {
			PUT(cur, (unsigned int)(bp));
			PUT((char *)bp, 0);
			PUT((char *)(bp)+WSIZE, (unsigned int)((char *)(cur)+WSIZE));
		}
		else {
			PUT((char *)(bp)+WSIZE, (unsigned int)((char *)(cur)+WSIZE));
			PUT((char *)(bp), GET(cur));
			PUT((char *)(GET(cur))+WSIZE, (unsigned int)((char *)(bp)+WSIZE));
			PUT(cur, (unsigned int)(bp));
		}
	}
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
	if(ptr==NULL) return mm_malloc(size);			// original pointer is null, allocate a new space
	else if(size==0) {
		mm_free(ptr); return NULL;					// free the block
	}
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    size_t oldsize = GET_SIZE(HDRP(oldptr));
    size_t asize;
    // adjust block size to include overhead and alignment reqs.
    if(size <= DSIZE)
		asize = 2*DSIZE;
	// round up
	else asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
	
	// if old size and new size are close, no need to reallocate
	if(asize==oldsize || ((oldsize>asize)&&(oldsize-asize)<16)) {
		return ptr;
	}
	// if old size is greater than new size, separate the old block
	else if(oldsize>asize) {	
		PUT(FTRP(ptr), PACK(oldsize-asize, 0));
		PUT((char *)(ptr)+asize-WSIZE, PACK(oldsize-asize, 0));
		PUT(HDRP(ptr), PACK(asize, 1));
		PUT(FTRP(ptr), PACK(asize, 1));
		char *p = (char *)(ptr)+asize;
		connect(p);
		return ptr;
	}
	// else need to allocate a new block
	else {
		size_t nxt_size =  GET_SIZE(HDRP(NEXT_BLKP(ptr))); 			// check if we can use the next block directly
		
		// if next block is free and the size is greater than requested size
		if(GET_ALLOC(HDRP(NEXT_BLKP(ptr)))==0 && nxt_size+oldsize>=asize) {
			void *nxt_p = NEXT_BLKP(ptr);
			cut(nxt_p);							// cut off the connections with other free blocks
			
			// if the size of spare block <16, keep it as internal fragmentation
			if(nxt_size+oldsize-asize<16) {
				PUT(HDRP(ptr), PACK(nxt_size+oldsize, 1));
				PUT(FTRP(ptr), PACK(nxt_size+oldsize, 1));
			}
			// else separate it as new free block
			else {
				PUT(HDRP(ptr), PACK(asize, 1));
				PUT(FTRP(ptr), PACK(asize, 1));
				void *free_p = (void *)((char *)ptr+asize);
				PUT(HDRP(free_p), PACK(nxt_size+oldsize-asize, 0));
				PUT(FTRP(free_p), PACK(nxt_size+oldsize-asize, 0));
				connect(free_p); 				// insert the new free block into segregated free list
			}
			return ptr;
		}
		// allocate a new block and move all data from old block to new block
		else {
			newptr = mm_malloc(size);
		    copySize = GET_SIZE(HDRP(oldptr));
		    memcpy(newptr, oldptr, copySize-DSIZE);
		    mm_free(oldptr);
		    return newptr;
		}
	}
    

}














