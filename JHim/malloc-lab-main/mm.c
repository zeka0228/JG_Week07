//segregate
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



#define GET_PTR(p) (*(void **)(p))
#define PUT_PTR(p, val) (*(void **)(p) = (val))

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
static int find_IDX(size_t size);
static void UL(char *bp, char *SP);
static void AL(char *bp);
static char *heap_listp;




/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(24*WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0); //Alignment Padding
    PUT(heap_listp + (1*WSIZE), PACK(11 * DSIZE, 1)); //Prologue Header

    //2~21WSIZE까진 Linked List
    for(int i = 2; i<=21; i++){
        PUT_PTR(heap_listp + (i*WSIZE), NULL);
    }

    PUT(heap_listp + (22*WSIZE), PACK(11 * DSIZE, 1)); //Prologu footer

    //에필로그는 사이즈는 0이나 실제 차지는 워드 => 해당 모순성으로 에필로그인지 판별함
    PUT(heap_listp + (23*WSIZE), PACK(0, 1));     //Epilogue header
    heap_listp += WSIZE;
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}


static void *extend_heap(size_t words){

    char *bp;
    size_t size;

    
    
    //8바이트 정렬, 홀수면 4바이트가 따로 논다는거니 워드 하나 더 붙여주기
    size = (words % 2) ? (words +1) * WSIZE : words * WSIZE;
    
    if (size <= 2 * DSIZE)
        size = 2 * DSIZE;

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
        AL(bp);
 
        return bp;
    }

    else if(prev_alloc && !next_alloc){
        char *NT = NEXT_BLKR(bp);
        size_t NTS = GETSIZE(HDRP(NT));

        UL(NT, heap_listp + (find_IDX(NTS)*WSIZE));
        size += NTS;

        PUT(HDRP(bp), PACK(size, 0));

        //헤더값이 최신화 됐으니, 블록 사이즈가 이미 증가된 상태 = FTRP로 점프 뛸 떄 최신화 된 값으로 점프
        PUT(FTRP(bp), PACK(size, 0));

        
        
    }

    else if(!prev_alloc && next_alloc){
        char *PR = PREV_BLKR(bp);
        size_t PRS = GETSIZE(HDRP(PR));
        UL(PR, heap_listp + (find_IDX(PRS)*WSIZE));


        size += PRS;
        
        PUT(FTRP(bp), PACK(size,0));
        PUT(HDRP(PR), PACK(size,0));
        bp = PR;
       
    }
    
    else{
        char *NT = NEXT_BLKR(bp);
        char *PR = PREV_BLKR(bp);
        size_t PRS = GETSIZE(HDRP(PR));
        size_t NTS = GETSIZE(HDRP(NT));
        UL(NT, heap_listp + (find_IDX(NTS)*WSIZE));
        UL(PR, heap_listp + (find_IDX(PRS)*WSIZE));
        
        size += PRS + NTS;

        PUT(FTRP(NT), PACK(size,0));
        PUT(HDRP(PR), PACK(size,0));
        
        bp = PR;
    }

    AL(bp);
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
}




//size대로 검색, 없으면 ++, 사이즈 조절을 여기서 해야하나? 
static void *find_fit(size_t asize){
    
    int idx = find_IDX(asize);

    char *List_CP = heap_listp + (idx * WSIZE);
    char *bp;

    //일단 따로 리미트 없이 while, 최적화 때 조절 필요요
    while (idx <= 20)
    {   
        if((bp = GET_PTR(List_CP)) != NULL){

            // 32kb까진 FF 설정, 최적화 때 조절 필요
            if(idx <= 12){
                // UL(bp, List_CP);
                //FF
                for(; bp != NULL; bp = GET_PTR(bp+WSIZE)){
                    if(GETSIZE(HDRP(bp)) >= asize)
                        return bp;
                }
            }

            //32kb 이후부턴 BF 설정, 최적화 때 조절 필요요
            else{
                char *best_bp = NULL;
                for(; bp != NULL; bp = GET_PTR(bp+WSIZE)){

                    size_t cur = GETSIZE(HDRP(bp));
                    
                    if(cur == asize) 
                        return bp;
                    
                    if(cur > asize){
                        if(best_bp == NULL) 
                            best_bp = bp;
                        
                        else{
                            if(cur < GETSIZE(HDRP(bp)))
                                best_bp = bp;
                        }

                    }
                }

                // UL(best_bp, List_CP);
                if(best_bp != NULL) return best_bp;
            }
        }

        idx++;
        List_CP += WSIZE;
    }
    
    return NULL;
}

//GET PTR할떄마다 메모리 읽는 거니까 지역 변수로 저장해둬서 최소로 줄여야함
static void UL(char *bp, char *SP){
    
    char *prv = GET_PTR(bp);          
    char *suc = GET_PTR(bp + WSIZE);   

    if(prv == SP) 
        PUT_PTR(SP, suc);
    else 
        PUT_PTR(prv + WSIZE, suc);      

    if (suc)
        PUT_PTR(suc, prv);           

    return;
}

static void AL(char *bp){
    char *list_P = heap_listp + (find_IDX(GETSIZE(HDRP(bp))) * WSIZE);
    char *old = GET_PTR(list_P);  

    PUT_PTR(bp,list_P);
    PUT_PTR(bp + WSIZE, old);
    if(old){
        PUT_PTR(old, bp);
    }
    PUT_PTR(list_P,bp);
    return;
}


//size 바이트로 넘겨줘야됨 not DW 
static int find_IDX(size_t size){
    size --;


    int L2 = (int)(8 * sizeof(size) - __builtin_clz(size) -3);
    //원래 사이즈가 16KB 이하일 시
    if(L2 <= 11){
        return L2 ;
    }

    // 8kb의 배수로 줄이기 + 8kb만큼 뺴서 기준점 세팅
    int w = (size >> 13)-1;

    
    //일단 머리 쥐어짜서 만들긴 했음 잠꺠면 다시 좀 봐줘라
    //16KB~64KB : 8KB씩 건너뛰기
    if (w <= 5)      
        return 11 + w;

    //64KB~96KB : 16KB씩 건너뛰기
    if ((w >>= 1) <= 5)
        return 13 + w;
    
    //128KB 이상
    if (w > 7)    
        return 20;

    //96~128KB
    return 19; 



}










static void place(void *bp, size_t asize){
    size_t checker = GETSIZE(HDRP(bp));
    char *check = heap_listp + (find_IDX(checker) * WSIZE);
    
    UL(bp, check);
    // 1블럭 이상의 free 블록이 나올 수 있는지 체크(헤더 4바, 푸터 4바, 본블럭 8바)
    if((checker-asize) >= (2*DSIZE)){
        
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));    
        bp = NEXT_BLKR(bp);
        PUT(HDRP(bp), PACK(checker-asize,0));
        PUT(FTRP(bp), PACK(checker-asize,0));
        AL(bp);
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

    if (ptr == NULL) {
        return mm_malloc(size);
    }
   
    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

  
    size_t asize;
    if (size <= DSIZE)                     
        asize = 2 * DSIZE;
    else                                   
        asize = ALIGN(size + (DSIZE));


   
    size_t csize   = GETSIZE(HDRP(ptr));   
   
   
    if (asize <= csize) {
        return ptr;
    }

    char *NT = NEXT_BLKR(ptr);        
    char *PR = PREV_BLKR(ptr);
    size_t prev_alloc = GETALLOC(FTRP(PREV_BLKR(ptr)));
    size_t next_alloc = GETALLOC(HDRP(NEXT_BLKR(ptr)));
    size_t NTS = GETSIZE(HDRP(NT));
    size_t PRS = GETSIZE(HDRP(PR));

    if(prev_alloc && !next_alloc && csize + NTS >= asize){
        

        UL(NT, heap_listp + (find_IDX(NTS)*WSIZE));
        csize += NTS;

        PUT(HDRP(ptr), PACK(csize, 1));
        PUT(FTRP(ptr), PACK(csize, 1));

        return ptr;
    }

    else if(!prev_alloc && next_alloc && csize + PRS >= asize){

        UL(PR, heap_listp + (find_IDX(PRS)*WSIZE));

        csize += PRS;

        PUT(FTRP(ptr), PACK(csize,1));
        PUT(HDRP(PR), PACK(csize,1));
        memmove(PR, ptr, csize - DSIZE);
        
        return PR;
    }
    
    else if(!prev_alloc && !next_alloc && csize + + NTS + PRS >= asize){

        UL(NT, heap_listp + (find_IDX(NTS)*WSIZE));
        UL(PR, heap_listp + (find_IDX(PRS)*WSIZE));
        
        csize += PRS + NTS;

        PUT(FTRP(NT), PACK(csize,1));
        PUT(HDRP(PR), PACK(csize,1));
        memmove(PR, ptr, csize - DSIZE);
        return PR;
    }
    
  
    void *newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;

 
    size_t copySize = csize - DSIZE;        
    if (size < copySize)
        copySize = size;
    memcpy(newptr, ptr, copySize); 
    
    mm_free(ptr); 
    return newptr;
}