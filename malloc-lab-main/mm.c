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
static void *find_nextp;                   // 다음 가용 블록을 탐색하기 위한 포인터 (next_fit)

static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDRP(ptr));

    if (prev_alloc && next_alloc) /* Case 1 */
    {
        return ptr;
    }

    if (prev_alloc && !next_alloc) /* Case 2 */
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(size, 0));
        PUT(FTRP(ptr), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) /* Case 3 */
    {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr)));
        PUT(FTRP(ptr), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    else /* Case 4 */
    {
        size += GET_SIZE(HDRP(PREV_BLKP(ptr))) +
                GET_SIZE(FTRP(NEXT_BLKP(ptr)));
        PUT(HDRP(PREV_BLKP(ptr)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(size, 0));
        ptr = PREV_BLKP(ptr);
    }
    find_nextp = ptr;
    return ptr;
}

static void *extend_heap(size_t words)
{
    char *ptr;
    size_t size;

    // 확장할 크기를 정렬 요구 사항에 맞게 조정
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(ptr = mem_sbrk(size)) == -1)
        return NULL;

    // 새로 확장된 영역의 프리 블록 헤더와 푸터, 그리고 새 에필로그 헤더 초기화
    PUT(HDRP(ptr), PACK(size, 0));         // 프리 블록 헤더
    PUT(FTRP(ptr), PACK(size, 0));         // 프리 블록 푸터
    PUT(HDRP(NEXT_BLKP(ptr)), PACK(0, 1)); // 새 에필로그 헤더

    // 인접한 프리 블록과의 병합을 시도하여 메모리 단편화 감소
    // 코얼레스 (코알라, 코머시기)
    return coalesce(ptr);
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) // 초기 힙 메모리를 할당
        return -1;

    PUT(heap_listp, 0);                            // 힙의 시작 부분에 0을 저장하여 패딩으로 사용
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 블럭의 헤더에 할당된 상태로 표시하기 위해 사이즈와 할당 비트를 설정하여 값을 저장
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // 프롤로그 블록의 풋터에도 마찬가지로 사이즈와 할당 비트를 설정하여 값을 저장
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     // 에필로그 블록의 헤더를 설정하여 힙의 끝을 나타내는 데 사용
    heap_listp += (2 * WSIZE);                     // 프롤로그 블록 다음의 첫 번째 바이트를 가리키도록 포인터 조정
    find_nextp = heap_listp;                       // nextfit을 위한 변수

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) // 초기 힙을 확장하여 충분한 양의 메모리가 사용 가능하도록 chunksize를 단어 단위로 변환하여 힙 확장
        return -1;
    if (extend_heap(4) == NULL)                  //자주 사용되는 작은 블럭이 잘 처리되어 점수가 오름
        return -1;
    return 0;
}

static void *find_fit(size_t asize)
{
    /* Next-fit search */
    void *ptr;
    ptr = find_nextp;
    // 현재 블록이 에필로그 블록이 아닌 동안 계속 순회, 블록의 헤더 크기가 0보다 크지 않으면 에필로그 블럭
    for (; GET_SIZE(HDRP(find_nextp)) > 0; find_nextp = NEXT_BLKP(find_nextp))
    {
        // 가용 블럭의 헤더가 할당되어 있지 않고 요청된 크기보다 크거나 같은 경우 해당 가용 블록을 반환
        if (!GET_ALLOC(HDRP(find_nextp)) && (asize <= GET_SIZE(HDRP(find_nextp))))
        {
            return find_nextp;
        }
    }
    // 위의 for루프에서 가용 블럭을 찾지 못한 경우, 다시 순회
    for (find_nextp = heap_listp; find_nextp != ptr; find_nextp = NEXT_BLKP(find_nextp))
    { // 이전에 탐색했던 find_nextp 위치에서부터 다시 가용 블록을 찾아서 반환
        if (!GET_ALLOC(HDRP(find_nextp)) && (asize <= GET_SIZE(HDRP(find_nextp))))
        {
            return find_nextp;
        }
    }

    return NULL;
}

static void place(void *ptr, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(ptr)); // 현재 블록의 크기를 알아냄

    // 남은 공간이 충분히 클 경우, 즉 요청한 크기(asize)와 현재 크기(csize)의 차이가
    // 두 배의 더블 사이즈(DSIZE)보다 크거나 같으면 블록을 나눔
    if ((csize - asize) >= (2 * DSIZE))
    {
        // 현재 블록을 나누어 하나는 할당하고, 나머지 블록은 가용 리스트로 넘김
        PUT(HDRP(ptr), PACK(asize, 1));               // 요청된 크기로 헤더 설정
        PUT(FTRP(ptr), PACK(asize, 1));               // 요청된 크기로 풋터 설정
        ptr = NEXT_BLKP(ptr);                         // 나머지 블록으로 포인터 이동
        PUT(HDRP(ptr), PACK(csize - asize, 0));       // 나머지 블록의 헤더를 가용 블록으로 설정
        PUT(FTRP(ptr), PACK(csize - asize, 0));       // 나머지 블록의 풋터를 가용 블록으로 설정
    }
    else
    {
        // 남은 공간이 충분하지 않으면 블록을 나누지 않고 전체 크기를 할당
        PUT(HDRP(ptr), PACK(csize, 1));               // 전체 크기만큼 할당
        PUT(FTRP(ptr), PACK(csize, 1));               // 전체 크기만큼 할당
    }
}

/*
 * mm_malloc - general size t_malloc allocation function
 */ 
 
void *mm_malloc(size_t size) {
    size_t asize;        /* 조정된 요청 크기 */
    size_t extendsize;   /* 필요한 메모리 크기 */
    char *ptr;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = ALIGN(size + DSIZE);

    /* First fit search for a fit */
    if ((ptr = find_fit(asize)) != NULL)
    {
        place(ptr, asize);
        return ptr;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((ptr = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(ptr, asize);
    return ptr;
}

void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));      /* 블록을 가용 블록으로 설정 */
    PUT(FTRP(ptr), PACK(size, 0));      /* 풋터도 가용 블록으로 설정 */
    coalesce(ptr);                      /* 주변의 가용 블록을 병합 */
}

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
