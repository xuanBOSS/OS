// user/umalloc.c
#include "user.h"

typedef uint64 Align;

union header {
    struct {
        union header *ptr;
        uint32 size;
    } s;
    Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void free(void *ap) {
    Header *bp, *p;
    
    if (ap == NULL) return;
    
    bp = (Header*)ap - 1;
    for (p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
        if (p >= p->s.ptr && (bp > p || bp < p->s.ptr))
            break;
    
    if (bp + bp->s.size == p->s.ptr) {
        bp->s.size += p->s.ptr->s.size;
        bp->s.ptr = p->s.ptr->s.ptr;
    } else
        bp->s.ptr = p->s.ptr;
    
    if (p + p->s.size == bp) {
        p->s.size += bp->s.size;
        p->s.ptr = bp->s.ptr;
    } else
        p->s.ptr = bp;
    
    freep = p;
}

static Header* morecore(uint32 nu) {
    char *p;
    Header *hp;
    
    if (nu < PGSIZE/sizeof(Header))
        nu = PGSIZE/sizeof(Header);
    p = sbrk(nu * sizeof(Header));
    if (p == SBRK_ERROR)
        return NULL;
    hp = (Header*)p;
    hp->s.size = nu;
    free((void*)(hp + 1));
    return freep;
}

void* malloc(uint32 nbytes) {
    Header *p, *prevp;
    uint32 nunits;

    printf("DEBUG: malloc called with nbytes=%d\n", nbytes);
    
    nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;
    printf("DEBUG: nunits=%d\n", nunits);

    if ((prevp = freep) == NULL) {
        printf("DEBUG: initializing malloc, freep is NULL\n");
        base.s.ptr = freep = prevp = &base;
        base.s.size = 0;
    }

    printf("DEBUG: starting search loop\n");
    for (p = prevp->s.ptr; ; prevp = p, p = p->s.ptr) {
        printf("DEBUG: checking block at %p, size=%d\n", p, p->s.size);
        if (p->s.size >= nunits) {
            printf("DEBUG: found suitable block\n");
            if (p->s.size == nunits)
                prevp->s.ptr = p->s.ptr;
            else {
                p->s.size -= nunits;
                p += p->s.size;
                p->s.size = nunits;
            }
            freep = prevp;
            printf("DEBUG: malloc returning %p\n", (void*)(p + 1));
            return (void*)(p + 1);
        }
        if (p == freep) {
            printf("DEBUG: no suitable block found, calling morecore\n");
            if ((p = morecore(nunits)) == NULL) {
                printf("DEBUG: morecore failed\n");
                return NULL;
            }
            printf("DEBUG: morecore returned %p\n", p);
        }
    }
}
