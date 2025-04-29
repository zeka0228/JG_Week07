/*
 * mm_segregated.c – Explicit segregated free‑list malloc package
 * --------------------------------------------------------------
 *  ▸ Segregated lists (16 size‑class buckets, 8‑byte granularity)
 *  ▸ Address‑ordered within each bucket (first‑fit)
 *  ▸ Immediate coalescing on free / extend_heap
 *  ▸ Realloc tries in‑place grow (prev/next) before malloc‑copy
 *  ▸ Runs with CS:APP malloc‑lab mdriver (64‑bit)
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <assert.h>
 #include "mm.h"
 #include "memlib.h"
 
 /*************************
  *  팀 정보
  *************************/
 team_t team = {
     "Team 7",
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

/* 함수 프로토타입 */
int mm_init(void); // 초기 힙 생성 -> 성공 0 / 실패 -1 리턴
void *mm_malloc(size_t size); // brk 포인터를 증가시키면서 요청 바이트를 수용할 블록 할당 
void mm_free(void *ptr); // 더이상 필요 없는 블록을 가용 상태로 전환 
void *mm_realloc(void *ptr, size_t size); // 같은 자리에서 확장/축소 시도 후 안 되면 새 블록 할당->데이터 복사->옛 블록 free
static void *extend_heap(size_t words); // 힙을 늘려 새 가용 블록을 만들고 필요시 인접 블록과 통합
static void *find_fit(size_t asize); // fit 정책에 따라서 asize 이상인 free 블록의 bp 탐색
static void place(void *bp, size_t asize); // free 블록에 asiz만큼 할당. 경우에 따라 split까지 수행
static void *coalesce(void *bp); // 가용 블록 연결
static void insert_node(void *bp, size_t size); // 가용 블록들로만 구성된 연결리스트에 bp가 가리키는 블록 삽입
static void remove_node(void *bp); // 가용 블록들로만 구성된 연결리스트에서 bp가 가리키는 블록 삭제

/*************************
 *  Segregated list buckets
 *************************/
#define LISTMAX 16                  /* 2^4 … 2^19 (32 -> 262k+) */
static void *seg_list[LISTMAX];
 
/* 
 * 사이즈에 맞게 버킷 인덱스 반환 
 */
static int find_idx(size_t sz) {
    int id = 0;
    // 사이즈가 1 이하가 되거나 인덱스가 최대 버킷에 도달할 때까지 
    while (id < LISTMAX - 1 && sz > 1) { 
        sz >>= 1; id++; // 사이즈를 2로 나누고, 인덱스를 1씩 증가
    }
    return id;
}

/* 
 * malloc 패키지 초기화 + 초기 힙 생성
 */
int mm_init(void)
{
    // 빈 버킷 초기화
    for (int i = 0; i < LISTMAX; i++) seg_list[i] = NULL;

    // mem_sbrk를 호출해 메모리 시스템에서 4워드를 가져와 힙(brk)을 그만큼 확장하고, 확장 전 포인터를 heap_listp에 저장 
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) return -1; 

    PUT(heap_listp, 0);                          // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // Prologue header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     // Epilogue header
    heap_listp += (2*WSIZE); // 힙의 시작 포인터 초기화

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

    // segregated list에서 적당한 가용 블록 탐색
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
void mm_free(void *bp)
{
    if (bp == NULL) return;

    size_t size = GET_SIZE(HDRP(bp)); // 현재 블록의 크기

    PUT(HDRP(bp), PACK(size, 0)); // 현재 블록의 헤더에 현재 블록의 크기만큼 미할당되었음을 설정
    PUT(FTRP(bp), PACK(size, 0)); // 현재 블록의 푸터에 현재 블록의 크기만큼 미할당되었음을 설정
    bp = coalesce(bp);
    insert_node(bp, GET_SIZE(HDRP(bp))); // ptr을 가용 블록 연결 리스트에 추가
}

/*
 * 현재 블록에서 요청받은 만큼 확장
 * 
 * 0. C 표준 realloc 정의에 따른 특수상황 처리
 *   1) ptr이 NULL 이면 malloc(size)
 *   2) size가 0이면 free(ptr)
 * 1. 요청 사이즈를 정렬 기준에 맞춰 재조정
 * 2. 아래 로직을 순서대로 수행 후 통과시 반환
 *   1) 요청 사이즈 <= 가용 사이즈: 현재 블록 사용 
 *   2) 요청 사이즈 <= 현재 가용 사이즈 + 다음 블록 가용 사이즈: 다음 블록 연결
 *   3) 요청 사이즈 <= 현재 가용 사이즈 + 이전 블록 가용 사이즈: 이전 블록 연결 + 이전 블록으로 데이터 이동
 *   4) 새 블록으로 이동
 */
void *mm_realloc(void *ptr, size_t size) {
    // 0. C 표준 realloc 정의에 따른 특수상황 처리
    if (ptr == NULL) { return mm_malloc(size); } // 1) ptr이 NULL 이면 realloc(NULL, size)은 malloc(size)과 동일
    if (size == 0)  { mm_free(ptr); return NULL; } // 2) size가 0 이면 realloc(ptr, 0)은 free(ptr)와 동일

    // 요청 사이즈 재조정 
    size_t asize = (size <= DSIZE) ? 2*DSIZE : ALIGN(size + DSIZE);
    
    // 1. 요청 사이즈가 할당 가능 사이즈보다 작거나 같으면 바로 반환
    size_t csize = GET_SIZE(HDRP(ptr));
    if (asize <= csize) return ptr; 
 
    // 2. 다음 블록과 연결 시 요청 사이즈보다 크거나 같으면 반환
    void *next = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDRP(next)) && (csize + GET_SIZE(HDRP(next)) >= asize)) {
        remove_node(next); // 다음 노드를 가용 블록 연결 리스트에서 제거
        size_t newsize = csize + GET_SIZE(HDRP(next));
        PUT(HDRP(ptr), PACK(newsize,1));
        PUT(FTRP(ptr), PACK(newsize,1));
        return ptr;
    }
 
    // 3. 이전 블록과 연결 시 요청 사이즈보다 크거나 같으면 반환 (+데이터 이동) */
    void *prev = PREV_BLKP(ptr);
    if (!GET_ALLOC(HDRP(prev)) && (csize + GET_SIZE(HDRP(prev)) >= asize)) {
        size_t prevsize = GET_SIZE(HDRP(prev));
        remove_node(prev); // 다음 노드를 가용 블록 연결 리스트에서 제거
        memmove(prev, ptr, csize - DSIZE);   // 💫 payload 복사 -> 데이터 이동
        PUT(HDRP(prev), PACK(prevsize + csize,1));
        PUT(FTRP(prev), PACK(prevsize + csize,1));
        return prev;
    }
 
    // 4. 실패 시 새로 메모리 할당 후 기존 블록 사이즈 복사
    void *newp = mm_malloc(size); 
    if (newp == NULL) return NULL;
    memcpy(newp, ptr, csize - DSIZE);
    mm_free(ptr);
    return newp;
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
    
    bp = coalesce(bp);
    insert_node(bp, GET_SIZE(HDRP(bp))); // 가용 블록 연결리스트에 해당 블록 추가
    return bp;
}
 
/*
 * 경계 태그 연결을 사용해서 상수 시간에 인접 가용 블록들과 통합
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
 
    if (prev_alloc && next_alloc) return bp;       /* case 1 */
 
    if (prev_alloc && !next_alloc) {               /* case 2 */
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));

    } else if (!prev_alloc && next_alloc) {        /* case 3 */
        remove_node(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));

    } else {                                       /* case 4 */
        remove_node(PREV_BLKP(bp));
        remove_node(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size,0));
        PUT(FTRP(bp), PACK(size,0));
    }
    return bp;
}

/* 
 * '주소 오름차순'으로 bp가 가리키는 노드를 해당 segregated list에 삽입
 */
static void insert_node(void *bp, size_t size) {
    // 올바른 버킷 선택
    int id = find_idx(size); 
    void *head = seg_list[id];
 
    // 주소 오름차순으로 삽입 위치 탐색
    void *prev = NULL, *cur = head;
    while (cur && cur < bp) {
        prev = cur;
        cur = SUCC(cur);
    }
    
    // bp의 포인터 필드 설정
    SUCC(bp) = cur;
    PRED(bp) = prev;

    // 현재 노드와 이전 노드, 다음 노드 연결 
    if (cur) PRED(cur) = bp; // q가 존재하면 q.pred = bp
    if (prev) SUCC(prev) = bp; // p가 존재하면 p.succ = bp, 존재하지 않으면 bucket head 갱신
    else seg_list[id] = bp;
}

/* 
 * segregated list에서 bp가 가리키는 노드 삭제
 */
static void remove_node(void *bp) {
    int id = find_idx(GET_SIZE(HDRP(bp)));

    // 이전 노드 연결 끊기
    if (PRED(bp)) SUCC(PRED(bp)) = SUCC(bp);
    else seg_list[id] = SUCC(bp);
    
    // 다음 노드 연결 끊기
    if (SUCC(bp)) PRED(SUCC(bp)) = PRED(bp);
}
 
/*
 * 적절한 segregated list 안에서 first-fit으로 가용블록 탐색 후 찾은 가용블록의 bp 반환
 */
static void *find_fit(size_t asize) {
    int id = find_idx(asize);
    for (int i = id; i < LISTMAX; i++) {
        void *bp = seg_list[i];
        for (; bp; bp = SUCC(bp)) {
            if (asize <= GET_SIZE(HDRP(bp))) return bp;
        }
    }
    return NULL;
}
 
/*
 * free 블록에 asiz만큼 할당. 필요시 split까지 수행
 */
static void place(void *bp, size_t asize) {
    size_t csize = GET_SIZE(HDRP(bp));
    remove_node(bp);
 
    if (csize - asize >= MINBLOCK) {
        PUT(HDRP(bp), PACK(asize,1));
        PUT(FTRP(bp), PACK(asize,1));
        void *next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(csize - asize,0));
        PUT(FTRP(next), PACK(csize - asize,0));
        insert_node(next, csize - asize);
    } else {
        PUT(HDRP(bp), PACK(csize,1));
        PUT(FTRP(bp), PACK(csize,1));
    }
}
 