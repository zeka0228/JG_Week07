// implicit-first&next_fit
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"
/* 팀 정보 */
team_t team = {
    "ateam", "Harry Bovik", "bovik@cs.cmu.edu", "", ""
};
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
static char *heap_listp = 0;  // 힙 시작 포인터
static void *next_fit;        // 다음 할당 위치 (Next-fit 전략용)
/* 함수 선언 */
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void add_free_block(void *bp);
static void splice_free_block(void *bp);

/* 메모리 관리자 초기화 */
int mm_init(void)
{   
    // 초기 힙 공간 할당 (4워드: 정렬 패딩 + 프롤로그 + 에필로그)
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)     // 초기 힙 메모리를 할당
        return -1;
     // 힙 구조 초기화
    PUT(heap_listp, 0);                             // 힙의 시작 부분에 0을 저장하여 패딩으로 사용
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    // 프롤로그 블럭의 헤더에 할당된 상태로 표시하기 위해 사이즈와 할당 비트를 설정하여 값을 저장
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));    // 프롤로그 블록의 풋터에도 마찬가지로 사이즈와 할당 비트를 설정하여 값을 저장
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));        // 에필로그 블록의 헤더를 설정하여 힙의 끝을 나타내는 데 사용
    heap_listp += (2*WSIZE);                        // 프롤로그 블록 다음의 첫 번째 바이트를 가리키도록 포인터 조정
    //find_nextp = heap_listp;                      // nextfit을 위한 변수(nextfit 할 때 사용)

    // 초기 힙 확장
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)       // 초기 힙을 확장하여 충분한 양의 메모리가 사용 가능하도록 chunksize를 단어 단위로 변환하여 힙 확장
        return -1;
    if (extend_heap(4) == NULL)                     //자주 사용되는 작은 블럭이 잘 처리되어 점수가 오름
        return -1;
    return 0;
}

/* extend_heap 새 가용 블록으로 힙 확장*/
static void *extend_heap(size_t words){
    char *ptr;
    size_t size;
    
     // 크기를 워드 단위로 조정 (짝수 개로 맞춤)
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(ptr = mem_sbrk(size)) == -1)
        return NULL;

    // 새 블록 헤더/풋터 설정
    PUT(HDRP(ptr), PACK(size, 0));          // 새 가용 블록 헤더
    PUT(FTRP(ptr), PACK(size, 0));          // 새 가용 블록 풋터
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1));  // 새로운 에필로그 헤더

    return coalesce(ptr);                   // 이전 가용 블록과 병합
}

/* 메모리 할당 함수 */
void *mm_malloc(size_t size)
{
    size_t asize;           // 조정된 블록 크기
    size_t extendsize;      // 힙 확장 크기
    char *ptr;

    // 크기 변환
    if (size == 0)
        return NULL;
    if (size <= DSIZE)      // 최소 블록 크기(16바이트) 보장
        asize = 2*DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);     // 크기 정렬
    
    // 적합한 가용 블록 검색
    if ((ptr = find_fit(asize)) != NULL){
        place(ptr, asize);
        return ptr;
    }

    // 적합한 블록 없을 시 힙 확장
    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(ptr, asize);
    return ptr;
}

/* 적합한 블록 탐색 (First-fit) */
static void *find_fit(size_t asize) {
    void *ptr = heap_listp;

    // 힙의 첫 번째 블록부터 마지막까지 검색
    while (GET_SIZE(HDRP(ptr)) > 0) {
        // 블록이 가용 블록인지 확인
        if (!GET_ALLOC(HDRP(ptr)) && GET_SIZE(HDRP(ptr)) >= asize) {
            return ptr; // 첫 번째 적합한 블록을 찾으면 반환
        }
        ptr = NEXT_BLKP(ptr); // 다음 블록으로 이동
    }
    return NULL; // 적합한 블록을 찾지 못한 경우 NULL 반환
}

/* 적합한 블록 탐색 (Next-fit) */
/*static void *find_fit(size_t asize) {
    void *ptr;
    ptr = find_nextp;   // 현재 탐색 시작 위치를 저장

    // 첫 번째 루프: 현재 find_nextp부터 힙 끝까지 탐색
    for (; GET_SIZE(HDRP(find_nextp)) >0; find_nextp = NEXT_BLKP(find_nextp))
    {   
        // 만약 블록이 할당되어 있지 않고, 크기가 요청한 크기 이상이면
        if (!GET_ALLOC(HDRP(find_nextp)) && (asize <= GET_SIZE(HDRP(find_nextp))))
        {
            return find_nextp;  // 해당 블록을 반환
        }
    }
    // 두 번째 루프: 힙 처음(heap_listp)부터 아까 저장해둔 ptr까지 탐색
    for (find_nextp = heap_listp; find_nextp != ptr; find_nextp = NEXT_BLKP(find_nextp))
    {   
        // 마찬가지로 할당되어 있지 않고 크기가 충분하면
        if (!GET_ALLOC(HDRP(find_nextp)) && (asize <= GET_SIZE(HDRP(find_nextp))))
        {
            return find_nextp;  // 해당 블록을 반환
        }
    }
    return NULL;    // 적합한 블록을 찾지 못한 경우
}*/

/* 블록 할당 및 분할 */
static void place(void *ptr, size_t asize) {
    size_t csize = GET_SIZE(HDRP(ptr));     // 현재 블록 크기

    // 남은 공간이 최소 블록 크기(16바이트) 이상인 경우 분할
    if (csize - asize >= (2*DSIZE)) { 
        // 블록을 나누어서 할당
        PUT(HDRP(ptr), PACK(asize, 1));  // 할당된 크기로 헤더 갱신
        PUT(FTRP(ptr), PACK(asize, 1));  // 할당된 크기로 푸터 갱신
        ptr = NEXT_BLKP(ptr);  // 나누어진 후, 새로 할당된 블록의 위치
        PUT(HDRP(ptr), PACK(csize - asize, 0));  // 나머지 블록은 가용 상태로 설정
        PUT(FTRP(ptr), PACK(csize - asize, 0));  // 나머지 블록의 푸터 갱신
    } else {
         // 전체 블록 할당
        PUT(HDRP(ptr), PACK(csize, 1));  // 전체 블록을 할당 상태로 설정
        PUT(FTRP(ptr), PACK(csize, 1));  // 전체 블록의 푸터 갱신
    }
}

/* 메모리 해제 함수 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    // 할당 비트 해제
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);  // 인접 가용 블록 병합
}

/* 가용 블록 병합 함수 */
static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));    // 이전 블록 할당 상태
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));    // 다음 블록 할당 상태
    size_t size = GET_SIZE(HDRP(ptr));                      // 현재 블록 크기
    /* case 1 : 이전, 다음 블록 모두 할당되어있다면 */
    if (prev_alloc && next_alloc){
        return ptr;
    }
    /* case 2 : 이전 블록은 할당되어있고, 다음 블록은 가용상태라면 */ 
    else if (prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));         // 다음 블록 크기 합치기
        PUT(HDRP(ptr), PACK(size,0));                   // 새 헤더 업데이트
        PUT(FTRP(ptr), PACK(size,0));                   // 새 풋터 업데이트
    }
    /* case 3 : 이전 블록은 가용상태이고, 다음 블록이 할당상태라면 */
    else if (!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));         // 이전 블록 크기 합치기
        PUT(FTRP(ptr), PACK(size,0));                   // 새 풋터 업데이트
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));       // 새 헤더 업데이트
        ptr = PREV_BLKP(ptr);                           // 포인터 이전 블록 시작점으로 이동
    }
    /* case 4 : 이전, 다음 블록 모두 가용 상태라면 */
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) +        // 양쪽 블록 크기 합치기
            GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));       // 새 헤더 업데이트
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));       // 새 풋터 업데이트
        ptr = PREV_BLKP(ptr);                           // 포인터 이전 블록 시작점으로 이동
    }
    return ptr;     // 병합된 블록 포인터 반환
}

/* 메모리 재할당 함수 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    // 새 블록 할당
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    
    // 복사할 데이터 크기 결정
    copySize = GET_SIZE(HDRP(oldptr));  // 원본 블록 크기
    if (size < copySize)                // 요청 크기가 더 작으면 조정
      copySize = size;

    memcpy(newptr, oldptr, copySize);   // 데이터 복사
    mm_free(oldptr);                    // 기존 블록 해제
    return newptr;
}