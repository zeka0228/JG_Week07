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
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

//사이즈 정의 (WSIZE = 4바이트의 헤더 /푸터 && DSIZE = 8바이트의 최소 블록 크기)
//헤더가 4바이트 = 2^32비트, 4GB보다 살짝 안되는 크기 정보까지는 저장가능능
#define WSIZE 4
#define DSIZE 8

//사이즈 정의 CHUNKSIZE = 할당 힙의 최소 크기(4KB)
#define CHUNKSIZE (1<<12)

//주어진 사이즈를 8의 배수로 올림 
//ex) 13을 받았으면 ALIGNMENT(=8) 13+(8-1) = 20, 10100 & ~(00111)= 10100 & 11000 = 10000 = 16  
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

//MAX 미리 만들기
#define MAX(x,y) ((x) > (y)? (x) : (y))


//PACK = size, alloc을 합치기, 하나라도 1이면 1 =  할당 모드(001)이면 size의 값이 변화
#define PACK(size, alloc)((size) | (alloc))

//GET = 주소 p에 저장된 값을 읽겠다(반환하겠다) 이때 주소 p = 헤더 / 푸터터
//PUT = 주소 p에 val의 값을 넣겠다 이때 주소 p = 헤더 / 푸터
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val)) 

//중간 세이브브


//주소 p에 저장된 값을 읽고, 이를 비트마스킹 = 사이즈 확보보
//주소 p의 1비트 자리 값을 읽고, alloc 여부 확인
#define GETSIZE(p) (GET(p) & ~0x7)
#define GETALLOC(p) (GET(p) & 0x1)


// 참고 : 힙 블럭 구조 [ header(4byte) | payload (bp) | ...[고오오옹 가아아아안]... | footer(4byte) ]
//bp = Base Point = payload의 시작 주소 
//HeaDeR Poiner
//HDRP = 헤더의 시작주소 = bp로부터 4바이트(WSIZE) 앞의 주소 반환
#define HDRP(bp) ((char *)(bp) - (WSIZE)) 

//FooTeR Pointer 
//FTRP = 해당 bp가 들어있는 블록 크기(헤더 bp 주소를 까서 그 안의 사이즈 값을 꺼냄) - footer크기 = footer의 시작 주소
//따라서 HDRP(bp)로 헤더 주소 확보, 그걸 GETSIZE로 안의 사이즈를 꺼내면 그게 블록 크기
//그 블록은 당연히 헤더 + 페이로드 + 푸터니까, 푸터의 크기만큼 뺴주면( -DSIZE ) => 그게 footer의 시작주소(=FTRP)
#define FTRP(bp) ((char *)(bp) + (GETSIZE(HDRP(bp))) - DSIZE)

//다음 블록 bp 찾기 = 지금 블록의 블록사이즈만큼 더하면 다음 bp  
#define NEXT_BLKR(bp) ((char *)(bp) + GETSIZE(((char *)(bp) - WSIZE)))

//이전블록의 footer에서 값을 뽑아야하니 bp - Wsize - Wsize = 이전bp footer, 여기서 나온 사이즈로 bp에서 빼주면 이전bp footer
#define PREV_BLKR(bp) ((char *)(bp) - GETSIZE(((char *)(bp) - DSIZE)))



static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);


static char *heap_listp;




/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); //Alignment Padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); //Prologue Header
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); //Prologu footer

    //에필로그는 사이즈는 0이나 실제 차지는 워드 => 해당 모순성으로 에필로그인지 판별함
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     //Epilogue header
    heap_listp += (2 * WSIZE);
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}


static void *extend_heap(size_t words){

    char *bp;
    size_t size;

    //8바이트 정렬, 홀수면 4바이트가 따로 논다는거니 워드 하나 더 붙여주기
    size = (words % 2) ? (words +1) * WSIZE : words * WSIZE;

    //mem_sbrk : 힙 size 증가 
    //확장 전 brk를 ord_brk로 따로 보관하고 리턴함, 즉 확장한 heap의 시작점을 리턴함
    if((long)(bp = mem_sbrk(size)) == -1) 
        return NULL;

    //해당 bp의 헤더 = bp - word = 이전 힙블록의 에필로그를 덮어씀 
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));

    //새 에필로그 헤더 재생성
    PUT(HDRP(NEXT_BLKR(bp)), PACK(0, 1));



    return coalesce(bp);
}


static void *coalesce(void *bp){
    size_t prev_alloc = GETALLOC(FTRP(PREV_BLKR(bp)));
    size_t next_alloc = GETALLOC(HDRP(NEXT_BLKR(bp)));
    size_t size = GETSIZE(HDRP(bp));
    
    if(prev_alloc && next_alloc){
        return bp;
    }

    else if(prev_alloc && !next_alloc){
        size += GETSIZE(HDRP(NEXT_BLKR(bp)));
        PUT(HDRP(bp), PACK(size, 0));

        //헤더값이 최신화 됐으니, 블록 사이즈가 이미 증가된 상태 = FTRP로 점프 뛸 떄 최신화 된 값으로 점프
        PUT(FTRP(bp), PACK(size, 0));
    }

    else if(!prev_alloc && next_alloc){
        size += GETSIZE(HDRP(PREV_BLKR(bp)));
        PUT(FTRP(bp), PACK(size,0));
        PUT(HDRP(PREV_BLKR(bp)),PACK(size,0));
        bp = PREV_BLKR(bp);
    }
    
    else{
        size += GETSIZE(HDRP(PREV_BLKR(bp))) + GETSIZE(HDRP(NEXT_BLKR(bp)));

        PUT(FTRP(NEXT_BLKR(bp)), PACK(size,0));
        PUT(HDRP(PREV_BLKR(bp)), PACK(size,0));
        bp = PREV_BLKR(bp);
    }

    return bp;
}






/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if(size == 0) 
        return NULL;


    if(size <= DSIZE)
        asize = 2*DSIZE;
    else
    asize = ALIGN(size + DSIZE);


    //find fit 으로 맞는 블록을 찾았을 때
    if((bp = find_fit(asize)) != NULL){
        place(bp,asize);
        return bp;
    }


    //못 찾았을 때
    extendsize = MAX(asize, CHUNKSIZE);
    if((bp = extend_heap(extendsize/WSIZE)) == NULL)   
        return NULL;
    place(bp,asize);
    return bp;

    
    //원래 코드 펼치기기
    // int newsize = ALIGN(size + SIZE_T_SIZE);
    // void *p = mem_sbrk(newsize);
    // if (p == (void *)-1)
	// return NULL;
    // else {
    //     *(size_t *)p = size;
    //     return (void *)((char *)p + SIZE_T_SIZE);
    // }
}



static void *find_fit(size_t asize){
    char *bp;

    //mem_heap_low 지점부터 시작해서, bp 헤더에서 뽑은 사이즈가 0보다 큰 동안(에필로그엔 0이 저장되어 있기에, 만나면 종료)
    for (bp = heap_listp; GETSIZE(HDRP(bp)) > 0; bp = NEXT_BLKR(bp)){
        if (!GETALLOC(HDRP(bp)) && GETSIZE(HDRP(bp)) >= asize)
            return bp;
    }
    return NULL;
}


static void place(void *bp, size_t asize){
    size_t checker = GETSIZE(HDRP(bp));

    // 1블럭 이상의 free 블록이 나올 수 있는지 체크(헤더 4바, 푸터 4바, 본블럭 8바)
    if((checker-asize) >= (2*DSIZE)){
        
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKR(bp);
        PUT(HDRP(bp), PACK(checker-asize,0));
        PUT(FTRP(bp), PACK(checker-asize,0));
    }
    else{
        PUT(HDRP(bp), PACK(checker, 1));
        PUT(FTRP(bp), PACK(checker, 1));
    }
    

}







/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GETSIZE(HDRP(ptr));
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
    copySize = GETSIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}











