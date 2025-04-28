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

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8 // 정렬을 위한 상수를 정의(8바이트로 정렬을 하겠다.)

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7) // 주어진 크기를 정렬에 맞게 조정하는 매크로

#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) // 자료형의 크기를 정렬에 맞게 조정하는 매크로

#define WSIZE 4             // 워드의 크기를 바이트 단위로 정의
#define DSIZE 8             // 더블 워드의 크기를 바이트 단위로 정의
#define CHUNKSIZE (1 << 12) // 초기 힙 확장에 사용되는 정크 크기를 정의 [2의 12승 (4096)]

/*주어진 두 값 중 큰 값을 반환하는 매크로*/
#define MAX(x, y) ((x) > (y) ? (x) : (y))
/*메모리 블록의 크기와 할당 비트를 결합하여 헤더 및 풋터에 저장할 값을 반환하는 매크로*/
#define PACK(size, alloc) ((size) | (alloc))

/*p가 가리키는 메모리를 unsigned int로 캐스팅한 뒤 해당 위치의 값을 반환
p가 가리키는 메모리를 unsigned int로 캐스팅한 뒤 해당 위치에 val값을 저장 */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/*주소 p에 있는 헤더 또는 풋터의 크기를 반환, 할당 비트를 제외하고 나머지 비트를 반환
주소 p에 있는 헤더 또는 풋터의 할당 비트를 반환, 할당 상태를 나타냄*/
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/*주어진 블록 포인터 ptr에 대한 헤더 주소를 반환, 주소 ptr에서 워드 크기만큼 뺀 주소를 반환
헤더 주소에서 해당 블록의 크기를 구한 뒤 더블 워드 크기를 배서 풋터의 주소를 반환*/
#define HDRP(ptr) ((char *)(ptr)-WSIZE)
#define FTRP(ptr) ((char *)(ptr) + GET_SIZE(HDRP(ptr)) - DSIZE)

/*현재 블록의 이전 블록 헤더로부터 크기를 읽어와 현재 블록의 끝 주소 다음을 반환
현재 블록의 이전 블록의 풋터로부터 크기를 읽어와 이전 블록의 시작 주소를 반환*/
#define NEXT_BLKP(ptr) ((char *)(ptr) + GET_SIZE(((char *)(ptr)-WSIZE)))
#define PREV_BLKP(ptr) ((char *)(ptr)-GET_SIZE(((char *)(ptr)-DSIZE)))

static void *coalesce(void *ptr);           // 주변의 가용 블록을 병합하여 하나의 블록으로 만드는 함수를 선언
static void *extend_heap(size_t words);    // 힙을 확장하는 함수를 선언
static void *heap_listp;                   // 가용 리스트의 시작을 나타내는 포인터
int mm_init(void);                         // 메모리 할당 시스템을 초기화하는 함수를 선언
static void *find_fit(size_t asize);       // 요청된 크기에 맞는 가용 블록을 탐색하는 함수를 선언
static void place(void *ptr, size_t asize); // 할당된 메모리 블록을 가용 리스트에서 제거하고 요청된 크기로 분할하는 함수를 선언
void *mm_malloc(size_t size);              // 주어진 크기의 메모리 블록을 할당하는 함수를 선언
void mm_free(void *ptr);                   // 이전에 할당된 메모리 블록을 해제하는 함수를 선언
void *mm_realloc(void *ptr, size_t size);  // 이전에 할당된 메모리 블록의 크기를 조정하거나 새로운 위치로 메모리를 이동하는 함수를 선언

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