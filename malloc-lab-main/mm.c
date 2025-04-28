/*
 * mm_lazy_explicit.c -- Explicit free-list malloc
 * ------------------------------------------------
 * Policies:
 *  ◦ Explicit free list (address-ordered)
 *  ◦ First-fit allocation
 *  ◦ Lazy coalescing: coalesce only on malloc failure
 *  ◦ Immediate splitting on placement
 *  ◦ No immediate coalesce on free (lazy)
 *
 * Blocks contain header/footer; free blocks carry pred/succ pointers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "7조",
    "안유진",
    "anewjean00@gmail.com",
    "",
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
#define MINBLOCK 24 // 가용 블록은 최소 24bytes: PRED(8 bytes) + SUCC(8 bytes) + Header(4 bytes) + Footer(4 bytes)

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
#define PRED(bp) (*(char **)(bp)) // 이전 가용 블록 포인터
#define SUCC(bp) (*(char **)((char *)(bp) + DSIZE)) // 다음 가용 블록 포인터

/* 포인터 */
static char *heap_listp; // 힙의 시작 포인터
static char *free_listp; // 가용 리스트 시작 포인터

/* 함수 프로토타입 */
int mm_init(void); // 초기 힙 생성 -> 성공 0 / 실패 -1 리턴
void *mm_malloc(size_t size); // brk 포인터를 증가시키면서 요청 바이트를 수용할 블록 할당 
void mm_free(void *ptr); // 더이상 필요 없는 블록을 가용 상태로 전환 
void *mm_realloc(void *ptr, size_t size); // 같은 자리에서 확장/축소 시도 후 안 되면 새 블록 할당->데이터 복사->옛 블록 free
static void *extend_heap(size_t words); // 힙을 늘려 새 가용 블록을 만들고 필요시 인접 블록과 통합
static void *find_fit(size_t asize); // fit 정책에 따라서 asize 이상인 free 블록의 bp 탐색
static void *place(void *bp, size_t asize); // free 블록에 asiz만큼 할당. 경우에 따라 split까지 수행
static void *coalesce_all(void); // lazy하게 모든 가용 블록을 coalesce
static void insert_node(void *bp); // 가용 블록들로만 구성된 연결리스트에 bp가 가리키는 블록 삽입
static void remove_node(void *bp); // 가용 블록들로만 구성된 연결리스트에서 bp가 가리키는 블록 삭제
 
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
    heap_listp += (2*WSIZE); // 힙의 시작 포인터 초기화
    free_listp = NULL; // 💫 가용 리스트 시작 포인터 초기화

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

    asize = MAX(asize, MINBLOCK); // 💫 asize가 24bytes보다 작다면 24bytes로 변경

    // 적당한 free list를 찾으면 bp에 asize 만큼의 블록 만들어주고 bp 반환
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // lazy하게 가용 블록들을 모두 coalesce
    coalesce_all();
    if ((bp = find_fit(asize))!=NULL) {
        return place(bp,asize);
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
    insert_node(ptr); // ptr을 가용 블록 연결 리스트에 추가
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

    asize = MAX(asize, MINBLOCK); // 💫 asize가 24bytes보다 작다면 24bytes로 변경

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
            remove_node(next_bp); // 💫 다음 노드를 가용 블록 연결 리스트에서 제거
            
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
    size_t size = words*WSIZE;

    if ((long)(bp=mem_sbrk(size))==-1) return NULL; // size만큼 brk를 올려주고, 올리기 전의 brk(== 새 블록의 시작)을 bp에 저장
    
    PUT(HDRP(bp),PACK(size,0)); // bp - 4 위치에 header
    PUT(FTRP(bp),PACK(size,0)); // bp + size - 8 위치에 footer
    PUT(HDRP(NEXT_BLKP(bp)),PACK(0,1)); // free 블록 뒤에 새로운 epilogue header 생성
    
    insert_node(bp); // 가용 블록 연결리스트에 해당 블록 추가
    return bp;
}

/* 
 * 특정 요청 시점에 인접한 가용 블록들을 모두 연결 
 */
static void *coalesce_all(void) {
    char *bp = free_listp;

    while (bp) {
        char *next = SUCC(bp);
        // 만약 bp 바로 다음(next)에 free 블록이 붙어 있으면 연결
        if (next && NEXT_BLKP(bp) == next) {
            // next를 연결할 것이므로 free_list에서 삭제
            remove_node(next);
            // size 합산, 헤더/푸터 갱신
            size_t new_size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(next));
            PUT(HDRP(bp), PACK(new_size, 0));
            PUT(FTRP(bp), PACK(new_size, 0));
            // bp가 존재할 때까지 계속 bp와 그 다음(next) 비교 (multi-merge)
        } else {
            // 인접하지 않으면 다음 블록으로 진행
            bp = next;
        }
    }
}

/* 
 * first-fit으로 가용블록 탐색 후 찾은 가용블록의 bp 반환
 * Explicit Free List이므로 전체 블록을 순회할 필요 없이 가용 블록 연결리스트만 확인한다.
 */
static void *find_fit(size_t asize) {
    for (char *bp = free_listp; bp; bp=SUCC(bp)) {
        if (asize<=GET_SIZE(HDRP(bp)))
            return bp;
    }
    return NULL;
}

/*
 * free 블록에 asiz만큼 할당. 필요시 split까지 수행
 */
static void *place(void *bp,size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp)); // bp가 가리키는 현재 가용 블록의 사이즈를 읽는다. 
    remove_node(bp); // 할당할 것이므로 가용 블록 연결리스트에서 삭제

    if (csize - asize >= MINBLOCK) { // '현재 블록 크기 - 요청 크기'가 24 bytes보다 크거나 같다면 
        PUT(HDRP(bp),PACK(asize,1)); // 현재 블록의 헤더에 요청 크기만큼 할당되었음을 설정
        PUT(FTRP(bp),PACK(asize,1)); // 현재 블록의 풋터에 요청 크기만큼 할당되었음을 설정
        void *r = NEXT_BLKP(bp); // bp를 다음 블록의 bp로 이동 ❗️할당받은 블록(bp)를 건드리지 않기 위해서 다음 블록 포인터를 따로 관리해야한다. 
        PUT(HDRP(r),PACK(csize-asize,0)); // 다음 블록의 헤더에 '현재 가용 크기 - 요청 크기'만큼 미할당되었음을 설정
        PUT(FTRP(r),PACK(csize-asize,0)); // 다음 블록의 풋터에 '현재 가용 크기 - 요청 크기'만큼 미할당되었음을 설정
        insert_node(r); // 다음 블록을 가용 블록 연결리스트에 삽입 
    } else {  // '현재 블록 크기 - 요청 크기'가 16 바이트보다 작다면 free 전체를 쓴다.
        PUT(HDRP(bp),PACK(csize,1)); // 현재 블록의 헤더에 전체 할당되었음을 설정
        PUT(FTRP(bp),PACK(csize,1)); // 현재 블록의 풋터에 전체 할당되었음을 설정
    }
    return bp; 
}
 
/* 
 * '주소 오름차순'으로 bp가 가리키는 노드를 연결리스트에 삽입
 */
static void insert_node(void *bp) {
    PRED(bp)=SUCC(bp)=NULL; // 이전과 다음 가용 블록 포인터 초기화
    char *p=NULL,*q=free_listp; // p: 삽입 위치 직전 노드, q: 순회 중인 노드 -> 초기화 
    while (q && q<bp) {  // bp보다 주소가 작은 노드를 지나쳐서 삽입 위치 찾기
        p=q;
        q=SUCC(q); 
    }
    // bp의 Predecessor, Successor 설정
    PRED(bp)=p;
    SUCC(bp)=q;

    // q가 존재하면 q.pred = bp
    if (q) PRED(q)=bp;

    // p가 존재하면 p.succ = bp, 존재하지 않으면 free_listp를 bp로 갱신
    if (p) SUCC(p)=bp;
    else free_listp=bp;
}
 
/* 
 * 가용 블록 연결리스트에서 bp가 가리키는 노드 삭제
 */
static void remove_node(void *bp) {
    char *p=PRED(bp),*q=SUCC(bp); // p: bp 직전 노드, q: bp 직후 노드 
    
    // 앞쪽 연결 끊기
    if (p) SUCC(p)=q; // p -> q
    else free_listp=q; // p가 NULL이면 bp가 리스트 헤드였던 것이므로 head 갱신
    
    // 뒷쪽 연결 끊기
    if (q) PRED(q)=p; // q <- p
}
