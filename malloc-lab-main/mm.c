/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "7조",
    /* First member's full name */
    "안유진",
    /* First member's email address */
    "anewjean@google.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8 // 주어진 사이즈를 8의 배수로 올림 ???

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7) // 할당

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4 // Word and header/footer size (bytes)
#define DSIZE 8 // Double word size (bytes)
#define CHUNKSIZE (1<<12) // Extend heap by this amount (bytes)

#define MAX(x, y) ((x) > (y) ? (x) : (y)) // x가 y보다 크면 x를, x가 y보다 작거나 같으면 y 반환

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc)) // size OR alloc(둘 중 하나라도 1이면 1) = 할당모드(001)이면 size 값 변화 ???

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p)) // read
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // write

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7) // 주소 p에 저장된 값을 읽고 비트마스킹 -> 블록 사이즈 확보
#define GET_ALLOC(p) (GET(p) & 0x1) // 주소 p의 1비트 자리값을 읽고 alloc 여부 확인

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp) - WSIZE) // 헤더 포인터 반환
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 푸터 포인터 반환

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 bp 찾기
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 bp 찾기

// 힙의 시작 포인터
static char *heap_listp;

int mm_init(void);
void *mm_malloc(size_t size);
void mm_free(void *ptr);
void *mm_realloc(void *ptr, size_t size);
static void *extend_heap(size_t words);
static void *coalesce(void *bp); 
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
// realloc();

/* 
 * mm_init - initialize the malloc package.
   Create the initial empty heap
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1;

    PUT(heap_listp, 0);                          // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // Epilogue header
    heap_listp += (2*WSIZE);

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1; // 힙을 CHUNKSIZE 바이트로 확장하고 초기 가용 블록을 생성
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
	// return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
    size_t asize;
    size_t extendsize;
    char *bp;

    /* Ignore spurious requests */
    if (size == 0) return NULL;

    /* Adjust block size to include overhead and alignment reqs */
    if (size <= DSIZE) {  // 할당해야할 사이즈가 DSIZE보다 작으면 오버헤드 포함할 수 있도록 2배로 할당
        asize = 2 * DSIZE;
    }

    else { // 할당해야할 사이즈가 DSIZE보다 크면 
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE); // ALIGNMENT 매크로 사용
    }

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr)); 
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

/*
새로운 가용 블록으로 힙 확장하기
*/
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); // Free block header
    PUT(FTRP(bp), PACK(size, 0)); // Free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // New epilogue header

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/*
경계 태그 연결을 사용해서 상수 시간에 인접 가용 블록들과 통합
*/
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // case 1
    if (prev_alloc && next_alloc) {
        return bp;
    }

    // case 2
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }

    // case 3
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    // case 4
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    return bp;
}

/*
first fit 검색을 수행하는 find_fit 함수
*/
static void *find_fit(size_t asize) {
    // // First-fit search - 처음 만나는 가용 블록 할당 
    // void *bp; // bp 선언

    // for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) { // heap_listp부터 순차적으로 블록을 훑으면서 
    //     if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) { // 할당되지 않은 블록 중 요청 크기보다 크거나 같은 블록을 찾으면
    //         return bp; // 해당 블록 바로 반환
    //     }
    // }
    // return NULL; // 못 찾으면 NULL

    // Next-fit search - 저번에 찾던 곳부터 이어서 검색
}
/*

*/
static void place(void *bp, size_t asize) {
    /*
    bp가 가리키는 free 블록이 있고, 요청한 크기 asize만큼 할당한다.
    만약 남는 공간이 16바이트(2 * DSIZE) 이상이면
    앞쪽은 할당 완료 표시, 뒤쪽은 새로운 free 블록으로 만들어준다. (split)
    남은 공간이 작으면 그냥 전체를 할당해버린다.
    */
    size_t csize = GET_SIZE(HDRP(bp)); // bp가 가리키는 'free 블록'의 현재크기를 읽는다. 

    if ((csize - asize >= (2*DSIZE))) { // 현재 블록 크기 - 요청 크기가 16 바이트보다 크거나 같다면 free가 충분히 크므로 free를 두개로 나눈다. 
        PUT(HDRP(bp), PACK(asize, 1));  // bp 블록의 헤더를 요청 크기, 할당됨으로 설정
        PUT(FTRP(bp), PACK(asize, 1));  // bp 블록의 풋터를 요청 크기, 할당됨으로 설정
        bp = NEXT_BLKP(bp); // bp를 다음 블록의 bp로 이동
        PUT(HDRP(bp), PACK(csize-asize, 0)); // 다음 블록의 헤더를 free - 할당 크기, '할당 안 됨'으로 설정
        PUT(FTRP(bp), PACK(csize-asize, 0)); // 다음 블록의 풋터를 free - 할당 크기, '할당 안 됨'으로 설정
    }
    else { // 현재 블록 크기 - 요청 크기가 16 바이트보다 작다면 free 전체를 쓴다.
        PUT(HDRP(bp), PACK(csize, 1)); //  bp 블록의 헤더를 free 크기, 할당됨으로 설정 (free만큼 모두 쓸것이기 때문에)
        PUT(FTRP(bp), PACK(csize, 1)); //  bp 블록의 풋터를 free 크기, 할당됨으로 설정
    }
}