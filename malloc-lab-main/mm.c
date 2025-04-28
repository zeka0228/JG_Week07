#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "7team",
    /* First member's full name */
    "hyosikkk",
    /* First member's email address */
    "gytlr0785@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* 매크로 상수 */
#define WSIZE 8
#define DSIZE 16
#define CHUNKSIZE (1 << 12)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
#define GET_SUCC(bp) (*(void **)((char *)(bp) + WSIZE))
#define GET_PRED(bp) (*(void **)(bp))
/* 전역 변수 */
static char *heap_listp;
/* 함수 선언 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void add_free_block(void *bp);
static void splice_free_block(void *bp);
/* mm_init - 초기화 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(8 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(2 * WSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(2 * WSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(4 * WSIZE, 0));
    PUT(heap_listp + (4 * WSIZE), 0);
    PUT(heap_listp + (5 * WSIZE), 0);
    PUT(heap_listp + (6 * WSIZE), PACK(4 * WSIZE, 0));
    PUT(heap_listp + (7 * WSIZE), PACK(0, 1));
    heap_listp += (4 * WSIZE);
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    if (extend_heap(4) == NULL)                     //자주 사용되는 작은 블럭이 잘 처리되어 점수가 오름
        return -1;
    return 0;
}
/* extend_heap - 힙 확장 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}
/* add_free_block - 가용 블록 free list 앞에 추가 */
static void add_free_block(void *bp)
{
    GET_SUCC(bp) = heap_listp;
    if (heap_listp != NULL)
        GET_PRED(heap_listp) = bp;
    GET_PRED(bp) = NULL;
    heap_listp = bp;
}
/* splice_free_block - free list에서 블록 제거 */
static void splice_free_block(void *bp)
{
    if (GET_PRED(bp))
        GET_SUCC(GET_PRED(bp)) = GET_SUCC(bp);
    else
        heap_listp = GET_SUCC(bp);
    if (GET_SUCC(bp))
        GET_PRED(GET_SUCC(bp)) = GET_PRED(bp);
}
/* coalesce - 인접 가용 블록 병합 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    if (prev_alloc && next_alloc) {
        add_free_block(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc) {
        splice_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        splice_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        splice_free_block(PREV_BLKP(bp));
        splice_free_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    add_free_block(bp);
    return bp;
}
/* find_fit - Best-Fit: 가장 작은 여유공간 찾기 */
static void *find_fit(size_t asize)
{
    void *bp;
    void *best = NULL;
    size_t best_size = (size_t)(-1);
    for (bp = heap_listp; bp != NULL; bp = GET_SUCC(bp)) {
        size_t bsize = GET_SIZE(HDRP(bp));
        if (!GET_ALLOC(HDRP(bp)) && bsize >= asize) {
            if (bsize < best_size) {
                best = bp;
                best_size = bsize;
            }
        }
    }
    return best;
}
/* place - 블록 배치 및 분할 */
static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    splice_free_block(bp);
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        add_free_block(bp);
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}
/* mm_malloc - 메모리 할당 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;
    if (size == 0)
        return NULL;
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}
/* mm_free - 메모리 해제 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}
/* mm_realloc - 메모리 재할당 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return mm_malloc(size);
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }
    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    size_t copySize = GET_SIZE(HDRP(ptr)) - DSIZE;
    if (size < copySize)
        copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}