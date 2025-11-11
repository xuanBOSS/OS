#include "lib/print.h"
#include "mem/str.h"
#include "lib/lock.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"

#ifndef offsetof
#define offsetof(type, member) ((size_t)&((type*)0)->member)
#endif

// 包装 mmap_region_t 用于仓库组织
typedef struct mmap_region_node {
    mmap_region_t mmap;
    struct mmap_region_node* next;
} mmap_region_node_t;

#define N_MMAP 256

// mmap_region_node_t 仓库(单向链表) + 指向链表头节点的指针 + 保护仓库的锁
static mmap_region_node_t list_mmap_region_node[N_MMAP];
static mmap_region_node_t* list_head;
static spinlock_t list_lk;

void mmap_init()
{
    spinlock_init(&list_lk, "mmap_list");
    
    // 初始化链表
    list_head = &list_mmap_region_node[0];
    for (int i = 0; i < N_MMAP - 1; i++) {
        list_mmap_region_node[i].next = &list_mmap_region_node[i + 1];
    }
    list_mmap_region_node[N_MMAP - 1].next = NULL;
}

mmap_region_t* mmap_region_alloc()
{
    spinlock_acquire(&list_lk);
    
    if (list_head == NULL) {
        spinlock_release(&list_lk);
        panic("mmap_region_alloc: no free regions");
    }
    
    mmap_region_node_t* node = list_head;
    list_head = list_head->next;
    
    spinlock_release(&list_lk);
    
    // 清零并返回
    memset(&node->mmap, 0, sizeof(mmap_region_t));
    return &node->mmap;
}

void mmap_region_free(mmap_region_t* mmap)
{
    if (mmap == NULL) return;
    
    mmap_region_node_t* node = (mmap_region_node_t*)((char*)mmap - offsetof(mmap_region_node_t, mmap));
    
    spinlock_acquire(&list_lk);
    node->next = list_head;
    list_head = node;
    spinlock_release(&list_lk);
}

void mmap_show_mmaplist()
{
    spinlock_acquire(&list_lk);
    
    mmap_region_node_t* tmp = list_head;
    int node = 1, index = 0;
    while (tmp) {
        index = tmp - list_mmap_region_node;
        printf("node %d index = %d\n", node++, index);
        tmp = tmp->next;
    }
    
    spinlock_release(&list_lk);
}
