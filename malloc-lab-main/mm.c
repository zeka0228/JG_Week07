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

/* 기본 상수와 매크로 */
#define ALIGNMENT 8 // 리스트 정렬 기준
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7) // 주어진 size에 'ALIGNMENT - 1'만큼 더한 뒤, 하위 3비트를 0으로 마스킹 -> size보다 크고 가장 작은 ALINGMENT의 배수 반환
#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // 헤더/푸터에 size_t 값을 담기 위해 8 bytes(ALIGNMENT) 단위로 올림-정렬한 최소 블록 크기
#define WSIZE 4 // 워드 사이즈 (bytes) -> 헤더, 푸터 사이즈로 사용
#define DSIZE 8 // 더블워드 사이즈 (bytes)
#define CHUNKSIZE (1<<12) // 힙 확장 단위 사이즈 4096 bytes(4KB)
#define MAX(x, y) ((x) > (y) ? (x) : (y)) // x가 y보다 크면 x를, x가 y보다 작거나 같으면 y 반환

/* 헤더/풋터 패킹/언패킹 */
#define PACK(size, alloc) ((size) | (alloc)) // size와 alloc을 1개의 4bytes 값으로 합쳐서 저장 - 헤더에 저장되는 값
#define GET(p) (*(unsigned int *)(p)) // 주소 p에 저장된 값 읽기
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // 주소 p에 val 값 쓰기
#define GET_SIZE(p) (GET(p) & ~0x7) // 주소 p에 저장된 값을 읽고 비트마스킹 -> 블록 사이즈 확보
#define GET_ALLOC(p) (GET(p) & 0x1) // 주소 p의 1비트 자리값을 읽고 alloc 여부 확인

/* 주소 계산 */
#define HDRP(bp) ((char *)(bp) - WSIZE) // 헤더 포인터 반환
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 푸터 포인터 반환
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 bp 찾기: 현재 블록 헤더에서 현재 블록 사이즈 찾아내서 현재 bp에서 그만큼 뒤로 간 값
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 bp 찾기: 이전 블록 푸터에서 이전 블록 전체 사이즈 알아내고 현재 bp에서 그만큼 앞으로 간 값

/* 포인터 */
static char *heap_listp; // 힙의 시작 포인터

/* 함수 프로토타입 */
int mm_init(void); // 초기 힙 생성 -> 성공 0 / 실패 -1 리턴
void *mm_malloc(size_t size); // brk 포인터를 증가시키면서 요청 바이트를 수용할 블록 할당 
void mm_free(void *ptr); // 더이상 필요 없는 블록을 가용 상태로 전환 
void *mm_realloc(void *ptr, size_t size); // 같은 자리에서 확장/축소 시도 후 안 되면 새 블록 할당->데이터 복사->옛 블록 free
static void *extend_heap(size_t words); // 힙을 늘려 새 가용 블록을 만들고 필요시 인접 블록과 통합
static void *coalesce(void *bp); // bp 주위의 전후 free 블록을 조건에 따라 합려서 더 큰 블록으로 통합된 블록의 bp 반환
static void *find_fit(size_t asize); // first-fit 정책에 따라서 asize 이상인 free 블록의 bp 탐색
static void place(void *bp, size_t asize); // free 블록에 asiz만큼 할당. 경우에 따라 split까지 수행

/* 
 * malloc 패키지 초기화 + 초기 힙 생성
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1; // mem_sbrk를 호출해 메모리 시스템에서 4워드를 가져와 힙(brk)을 그만큼 확장하고, 확장 전 포인터를 heap_listp에 저장 

    PUT(heap_listp, 0);                          // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // Epilogue header
    heap_listp += (2*WSIZE);

    // 힙을 CHUNKSIZE bytes/4 bytes (-> 워드 단위 환산)만큼 확장
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1; 
    return 0;
}

/* 
 * brk 포인터를 증가시키면서 블록 할당
 * 항상 ALIGNMENT의 배수만큼의 크기를 가지는 블록을 할당한다.
 */
void *mm_malloc(size_t size)
{
    size_t asize; // 할당 사이즈
    size_t extendsize; // 확장 사이즈
    char *bp; 

    if (size == 0) return NULL;

    // 오버헤드와 정렬 기준(8bytes)를 고려해서 블록 사이즈 재조정
    if (size <= DSIZE) {  // 요청 사이즈가 8bytes보다 작으면 헤더와 푸터로 인한 오버헤드를 고려해서 할당 사이즈를 16bytes로 
        asize = 2 * DSIZE;
    }

    else { // 요청 사이즈가 DSIZE보다 크면 
        asize = ALIGN(size + DSIZE); // + DSIZE는 헤더와 푸터로 인한 오버헤드
    }

    // 적당한 free list를 찾으면 bp에 asize 만큼의 블록 만들어주고 bp 반환
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 적당한 free list를 못 찾으면 힙 확장 후 bp에 asize 만큼의 블록 만들어주고 bp 반환
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * 아무것도 하지 않는 블록을 메모리에서 해제
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr)); // 현재 블록의 크기

    PUT(HDRP(ptr), PACK(size, 0)); // 현재 블록의 헤더에 현재 블록의 크기만큼 미할당되었음을 설정
    PUT(FTRP(ptr), PACK(size, 0)); // 현재 블록의 푸터에 현재 블록의 크기만큼 미할당되었음을 설정
    coalesce(ptr); // ptr 앞/뒤 확인해서 가용 블록 연결
}

/*
 * 현재 블록에서 요청받은 만큼 확장/축소 시도 후 안 되면 새로운 블록 할당->데이터 복사->예전 블록 free
 * 0. C 표준 realloc 정의에 따른 특수상황 처리*
 *   1) ptr이 NULL 이면 realloc(NULL, size)은 malloc(size)과 동일
 *   2) size가 0이면 free(ptr)와 동일 
 * 1. 요청 사이즈를 정렬 기준에 맞춰 재정렬
 * 2. 요청 사이즈와 현재 가용 사이즈에 따라 아래 셋 중 하나 수행
 *   1) 현재 블록 사용
 *   2) 인접 블록 합치기
 *   3) 새 블록으로 이동하기
 */
void *mm_realloc(void *ptr, size_t size)
{
    // 0. C 표준 realloc 정의에 따른 특수상황 처리
    // 1) ptr이 NULL 이면 realloc(NULL, size)은 malloc(size)과 동일
    if (ptr == NULL) {
        return mm_malloc(size);
    }
    // 2) size가 0 이면 realloc(ptr, 0)은 free(ptr)와 동일
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    // 1. 요청받은 크기를 정렬 기준에 따라 재조정
    size_t asize;
    if (size <= DSIZE)                      // 최소 16B
        asize = 2 * DSIZE;
    else                                    // 8B 정렬 + header/footer
        asize = ALIGN(size + (DSIZE));

    // 🧚🏻 현재 블록의 크기와 다음 블록의 포인터 저장
    size_t csize   = GET_SIZE(HDRP(ptr));   // 지금 블록의 전체 크기
    void *next_bp = NEXT_BLKP(ptr);        // 바로 다음 블록
    
    // 2. 현재 블록이 요청 사이즈보다 크면 그대로 반환
    if (asize <= csize) {
        return ptr;
    }

    // 3. 현재 블록이 충분히 크지 않다면
    if (!GET_ALLOC(HDRP(next_bp))) { // 뒤 블록이 free 이고, 합치면 충분히 큰지 체크
        size_t next_size = GET_SIZE(HDRP(next_bp));
        if (csize + next_size >= asize) {   // 3-1) 연결했을 때 충분히 크다면
            size_t newsize = csize + next_size;
            PUT(HDRP(ptr), PACK(newsize, 1));
            PUT(FTRP(ptr), PACK(newsize, 1));
            return ptr;                     // 복사없이 제자리 확장 완료
        }
    }

    // 3-2) 연결했을 때도 충분치 않다면 새로 할당 후 기존 블록 사이즈 복사
    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

    // payload 크기만 복사 (헤더/풋터 제외)
    size_t copySize = csize - DSIZE;        // csize 에는 header+footer 8B 포함되어있으므로 제외
    if (size < copySize)
        copySize = size;
    memcpy(newptr, ptr, copySize); // ptr → newptr로 payload 데이터 copy

    mm_free(ptr); // 기존 ptr은 메모리 할당 해제
    return newptr;
}

/*
 * 새로운 가용 블록으로 힙 확장하기
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    size = words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) return NULL; // size만큼 brk를 올려주고, 올리기 전의 brk(== 새 블록의 시작)을 bp에 저장

    PUT(HDRP(bp), PACK(size, 0)); // bp - 4 위치에 header
    PUT(FTRP(bp), PACK(size, 0)); // bp + size -8 위치에 footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // free 블록 뒤에 새로운 epilogue header 생성

    return coalesce(bp); // 인접 free 블록과 합치고 합쳐진 블록의 payload 포인터를 리턴
}

/*
 * 경계 태그 연결을 사용해서 상수 시간에 인접 가용 블록들과 통합
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
 * first fit으로 가용블록 탐색 후 찾은 가용블록의 bp 반환
 */
static void *find_fit(size_t asize) {
    void *bp;

    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) { // heap_listp부터 순차적으로 블록을 훑으면서 
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) { // 할당되지 않은 블록 중 요청 크기보다 크거나 같은 블록을 찾으면
            return bp; // 해당 블록 바로 반환
        }
    }
    return NULL; // 못 찾으면 NULL
}

/*
 * free 블록에 asiz만큼 할당 표시. 필요시 split까지 수행
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // bp가 가리키는 'free 블록'의 현재크기를 읽는다. 

    if ((csize - asize >= (2*DSIZE))) { // 현재 블록 크기 - 요청 크기가 16 바이트보다 크거나 같다면 free가 충분히 크므로 free를 두개로 나눈다. 
        PUT(HDRP(bp), PACK(asize, 1));  // 현재 블록의 헤더에 요청 크기만큼 할당되었음을 설정
        PUT(FTRP(bp), PACK(asize, 1));  // 현재 블록의 풋터에 요청 크기만큼 할당되었음을 설정
        bp = NEXT_BLKP(bp); // bp를 다음 블록의 bp로 이동
        PUT(HDRP(bp), PACK(csize-asize, 0)); // 다음 블록의 헤더에 '현재 가용 크기 - 요청 크기'만큼 미할당되었음을 설정
        PUT(FTRP(bp), PACK(csize-asize, 0)); // 다음 블록의 풋터에 '현재 가용 크기 - 요청 크기'만큼 미할당되었음을 설정
    }
    else { // 현재 블록 크기 - 요청 크기가 16 바이트보다 작다면 free 전체를 쓴다.
        PUT(HDRP(bp), PACK(csize, 1)); //  현재 블록의 헤더에 전체 할당되었음을 설정
        PUT(FTRP(bp), PACK(csize, 1)); //  현재 블록의 풋터에 전체 할당되었음을 설정
    }
}
