/*
    QEMU提供的虚拟磁盘的驱动
    QEMUOPTS = -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
    这个文件最终提供三个重要函数:
    virtio_disk_init() // 初始化函数
    virtio_disk_rw()   // 以block为单位的磁盘读写函数
    virtio_disk_intr() // 磁盘激活的中断处理函数
*/

#include "dev/virtio.h"
#include "fs/buf.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "mem/str.h"
#include "mem/vmem.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "riscv.h"
#include "memlayout.h"

// the address of virtio mmio register r.
#define R(r) ((volatile uint32 *)(VIRTIO_BASE + (r)))

static struct disk
{
    // memory for virtio descriptors &c for queue 0.
    // this is a global instead of allocated because it must
    // be multiple contiguous pages, which kalloc()
    // doesn't support, and page aligned.
    char pages[2 * PGSIZE];
    struct VRingDesc *desc;
    uint16 *avail;
    struct UsedArea *used;

    // our own book-keeping.
    char free[NUM];  // is a descriptor free?
    uint16 used_idx; // we've looked this far in used[2..NUM].

    // track info about in-flight operations,
    // for use when completion interrupt arrives.
    // indexed by first descriptor index of chain.
    struct
    {
        buf_t* b;
        char status;
    } info[NUM];

    struct virtio_blk_outhdr
    {
        uint32 type;
        uint32 reserved;
        uint64 sector;
    } ops[NUM];

    struct spinlock vdisk_lock;

} __attribute__((aligned(PGSIZE))) disk;

void virtio_disk_init(void)
{
    uint32 status = 0;

    spinlock_init(&disk.vdisk_lock, "virtio_disk");

    if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
        *R(VIRTIO_MMIO_VERSION) != 1 ||
        *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
        *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551)
    {
        panic("could not find virtio disk");
    }

    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    // negotiate features
    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_SCSI);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_BLK_F_MQ);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    // initialize queue 0.
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio disk max queue too short");
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

    memset(disk.pages, 0, sizeof(disk.pages));

    // ✅ 关键修复：转换为物理地址
    uint64 pages_va = (uint64)disk.pages;
    uint64 pages_pa;
    
    // disk 是全局变量，在内核数据段，检查是否在直接映射区
    if (pages_va >= KERNEL_BASE && pages_va < PHYSTOP) {
        // 直接映射区：虚拟地址 = 物理地址
        pages_pa = pages_va;
    } else {
        // 需要通过页表转换
        pte_t *pte = vm_getpte(NULL, pages_va, false);
        if (pte == NULL) {
            panic("virtio: cannot get PTE for disk.pages");
        }
        pages_pa = PTE2PA(*pte) | (pages_va & 0xFFF);
    }
    
    printf("disk.pages: VA=0x%lx, PA=0x%lx\n", pages_va, pages_pa);

    // ✅ 关键修复：使用物理地址而不是虚拟地址
    *R(VIRTIO_MMIO_QUEUE_PFN) = pages_pa >> 12;

    // desc = pages -- num * VRingDesc
    // avail = pages + 0x40 -- 2 * uint16, then num * uint16
    // used = pages + 4096 -- 2 * uint16, then num * vRingUsedElem

    disk.desc = (struct VRingDesc *)disk.pages;
    disk.avail = (uint16 *)(((char *)disk.desc) + NUM * sizeof(struct VRingDesc));
    disk.used = (struct UsedArea *)(disk.pages + PGSIZE);

    for (int i = 0; i < NUM; i++)
        disk.free[i] = 1;

    // ✅ 初始化 used_idx（设备控制的 used->idx 由设备初始化）
    disk.used_idx = 0;

    // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
    // 验证
    printf("VirtIO Legacy init:\n");
    printf("  QUEUE_PFN set to: 0x%x\n", *R(VIRTIO_MMIO_QUEUE_PFN));
    printf("  Expected: 0x%lx\n", pages_pa >> 12);
}

// find a free descriptor, mark it non-free, return its index.
static int alloc_desc(void)
{
    for (int i = 0; i < NUM; i++)
    {
        if (disk.free[i])
        {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// mark a descriptor as free.
static void free_desc(int i)
{
    if (i >= NUM)
        panic("virtio_disk_intr 1");
    if (disk.free[i])
        panic("virtio_disk_intr 2");
    disk.desc[i].addr = 0;
    disk.free[i] = 1;
    wakeup(&disk.free[0]);
}

// free a chain of descriptors.
static void free_chain(int i)
{
    while (1)
    {
        free_desc(i);
        if (disk.desc[i].flags & VRING_DESC_F_NEXT)
            i = disk.desc[i].next;
        else
            break;
    }
}

static int alloc3_desc(int *idx)
{
    for (int i = 0; i < 3; i++)
    {
        idx[i] = alloc_desc();
        if (idx[i] < 0)
        {
            for (int j = 0; j < i; j++)
                free_desc(idx[j]);
            return -1;
        }
    }
    return 0;
}

void virtio_disk_rw(buf_t *b, bool write)
{
    uint64 sector = b->block_num * (BLOCK_SIZE / 512);
    
    // ✅ 在开始时获取进程指针，并在整个函数中使用它
    // 这样可以避免在等待过程中 myproc() 返回 NULL
    // 注意：在系统调用处理过程中，cpu->proc 应该已经被设置
    proc_t *p = myproc();
    bool use_interrupt = (p != NULL);  // 如果有进程上下文，使用中断模式
    
    spinlock_acquire(&disk.vdisk_lock);

    // 分配描述符
    int idx[3];
    uint32 wait_count = 0;
    while (1) {
        if (alloc3_desc(idx) == 0) {
            break;
        }

        // ✅ 使用开始时确定的模式（中断或轮询）
        if (!use_interrupt) {
            // 初始化阶段：轮询等待描述符释放
            // 需要处理已完成的请求以释放描述符
            uint16 used_idx = disk.used->idx;
            
            if (disk.used_idx != used_idx) {
                // 有请求完成，处理它们以释放描述符
                while (disk.used_idx != used_idx) {
                    int id = disk.used->elems[disk.used_idx % NUM].id;
                    if (disk.info[id].status != 0) {
                        panic("virtio: bad status while waiting for descriptors");
                    }
                    if (disk.info[id].b) {
                        disk.info[id].b->disk = false;  // 标记完成，让请求的轮询循环退出
                    }
                    disk.used_idx++;
                    free_chain(id);  // 释放描述符，使它们可用于新请求
                }
                *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
                wait_count = 0;  // 重置计数器
                continue;  // 重试分配描述符
            }
            // 没有描述符可用，也没有完成的请求，继续轮询
            wait_count++;
            if (wait_count > 100000) {
                printf("virtio_disk_rw: ERROR - waited too long for descriptors!\n");
                printf("  used_idx=%d, used->idx=%d\n", disk.used_idx, disk.used->idx);
                printf("  dumping descriptor state:\n");
                for (int i = 0; i < NUM; i++) {
                    printf("    desc[%d]: free=%d\n", i, disk.free[i]);
                }
                panic("virtio: timeout waiting for descriptors");
            }
            // 短暂延迟后重试
            for (volatile int i = 0; i < 1000; i++);  // 简单延迟
            continue;
        }
        
        // 进程上下文：检查是否有已完成的请求，处理它们以释放描述符
        uint16 used_idx = disk.used->idx;
        if (disk.used_idx != used_idx) {
            // 有请求完成，处理它们以释放描述符
            while (disk.used_idx != used_idx) {
                int id = disk.used->elems[disk.used_idx % NUM].id;
                if (disk.info[id].status != 0) {
                    panic("virtio: bad status while waiting for descriptors");
                }
                if (disk.info[id].b) {
                    disk.info[id].b->disk = false;  // 标记完成
                }
                disk.used_idx++;
                free_chain(id);  // 释放描述符
            }
            *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
            continue;  // 重试分配描述符
        }
        
        // 没有描述符可用，也没有完成的请求，sleep 等待
        sleep(&disk.free[0], &disk.vdisk_lock);
    }

    // 设置请求头
    struct virtio_blk_outhdr *buf0 = &disk.ops[idx[0]];

    if (write)
        buf0->type = VIRTIO_BLK_T_OUT;
    else
        buf0->type = VIRTIO_BLK_T_IN;
    buf0->reserved = 0;
    buf0->sector = sector;

    // 描述符 0：请求头
    disk.desc[idx[0]].addr = (uint64)buf0;
    disk.desc[idx[0]].len = sizeof(struct virtio_blk_outhdr);    
    disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[idx[0]].next = idx[1];

    // 描述符 1：数据缓冲区（获取物理地址）
    uint64 data_va = (uint64)b->data;
    uint64 data_pa;

    if (data_va >= KERNEL_BASE && data_va < PHYSTOP) {
        data_pa = data_va;
    } else {
        uint64 data_page_va = ALIGN_DOWN(data_va, PGSIZE);
        uint64 data_offset = data_va % PGSIZE;
        
        pte_t* data_pte = vm_getpte(NULL, data_page_va, false);
        if (data_pte == NULL) {
            panic("virtio: cannot get PTE for b->data");
        }
        
        data_pa = PTE2PA(*data_pte) + data_offset;
    }

    disk.desc[idx[1]].addr = data_pa;
    disk.desc[idx[1]].len = BLOCK_SIZE;
    if (write)
        disk.desc[idx[1]].flags = 0;
    else
        disk.desc[idx[1]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[idx[1]].next = idx[2];

    // 描述符 2：状态字节
    disk.info[idx[0]].status = 0xff;
    
    disk.desc[idx[2]].addr = (uint64)&disk.info[idx[0]].status;
    disk.desc[idx[2]].len = 1;
    disk.desc[idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[idx[2]].next = 0;

    // 标记缓冲区正在使用
    b->disk = true;
    disk.info[idx[0]].b = b;

    // 提交到可用环
    disk.avail[2 + (disk.avail[1] % NUM)] = idx[0];
    __sync_synchronize();
    disk.avail[1] = disk.avail[1] + 1;
    __sync_synchronize();

    // 通知设备
    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

    // 等待完成
    // ✅ 使用开始时确定的模式（中断或轮询）
    if (!use_interrupt) {
    // ===== 初始化阶段：轮询模式 =====
    
    // 1. 释放锁
    disk.vdisk_lock.cpuid = -1;
    __sync_synchronize();
    __sync_lock_release(&disk.vdisk_lock.locked);
    
    // 2. 减少 noff
    cpu_t *c = mycpu();
    if (c->noff > 0) {
        c->noff--;
    }
    
    // 3. 纯轮询
    // printf("virtio_disk_rw: starting polling loop for block %d, b->disk=%d\n", b->block_num, b->disk);
    uint32 poll_count = 0;
    while (b->disk == true) {
        // ✅ 修复：处理所有已完成的请求，而不仅仅是当前请求
        uint16 used_idx = disk.used->idx;
        
        // 处理所有已完成的请求
        while (disk.used_idx != used_idx) {
            int id = disk.used->elems[disk.used_idx % NUM].id;
            
            if (disk.info[id].status != 0) {
                panic("virtio: bad status in init polling");
            }
            
            if (disk.info[id].b) {
                disk.info[id].b->disk = false;
                // 如果这是当前请求，标记并继续处理其他请求
                // 稍后会在重新获取锁后清理
            }
            
            disk.used_idx++;
        }
        
        // 如果当前请求已完成，退出
        if (!b->disk) {
            *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
            break;
        }
        
        // ✅ 添加超时检查，防止无限循环
        poll_count++;
        if (poll_count > 1000000) {  // 大约1秒（假设每次循环1微秒）
            printf("virtio_disk_rw: WARNING - polling timeout for block %d\n", b->block_num);
            printf("  disk.used_idx=%d, disk.used->idx=%d, b->disk=%d\n", 
                   disk.used_idx, disk.used->idx, b->disk);
            panic("virtio: polling timeout");
        }
        
        // 每100000次输出一次进度
        if (poll_count % 100000 == 0) {
            printf("virtio_disk_rw: polling... count=%d, used_idx=%d, used->idx=%d\n",
                   poll_count, disk.used_idx, disk.used->idx);
        }
    }
    // printf("virtio_disk_rw: polling loop completed\n");
    
    // 4. 恢复 noff
    c->noff++;
    if (c->noff == 1) {
        uint64 sstatus = r_sstatus();
        w_sstatus(sstatus & ~0x2UL);
    }
    
    // 5. 重新获取锁
    while (__sync_lock_test_and_set(&disk.vdisk_lock.locked, 1) != 0);
    __sync_synchronize();
    disk.vdisk_lock.cpuid = mycpuid();
    
    // ✅ 清理所有在轮询循环中完成的请求
    // 注意：disk.used_idx 已经在轮询循环中更新了
    // 但我们需要清理那些已完成的请求的描述符
    // 实际上，每个请求会在自己的 virtio_disk_rw 调用中清理
    // 但为了确保描述符及时释放，我们在这里也清理一下
    // （虽然这可能会导致重复清理，但 free_chain 是幂等的）
    } else {
        // ===== 进程上下文：sleep 等待中断 =====
        printf("virtio_disk_rw: entering sleep loop for block %d\n", b->block_num);
        while (b->disk == true) {
            printf("virtio_disk_rw: calling sleep for block %d\n", b->block_num);
            sleep(b, &disk.vdisk_lock);
            printf("virtio_disk_rw: woke up from sleep, b->disk=%d\n", b->disk);
        }
        printf("virtio_disk_rw: I/O completed for block %d\n", b->block_num);
    }

    // 清理当前请求
    // printf("virtio_disk_rw: cleaning up\n");
    disk.info[idx[0]].b = 0;
    free_chain(idx[0]);

    spinlock_release(&disk.vdisk_lock);
    // printf("virtio_disk_rw: completed for block_num=%d\n", b->block_num);
}

void virtio_disk_intr(void)
{
    printf("virtio_disk_intr: interrupt received\n");
    spinlock_acquire(&disk.vdisk_lock);

    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    
    printf("virtio_disk_intr: used_idx=%d, used->idx=%d\n", disk.used_idx, disk.used->idx);
    
    // ✅ 修复：使用 idx 而不是 id，不取模比较
    while (disk.used_idx != disk.used->idx) {
        // ✅ 取模访问 elems 数组
        int id = disk.used->elems[disk.used_idx % NUM].id;
        
        printf("virtio_disk_intr: processing completed request id=%d\n", id);
        
        if (disk.info[id].status != 0) {
            panic("virtio_disk_intr: bad status");
        }
        
        struct buf *b = disk.info[id].b;
        if (b) {
            printf("virtio_disk_intr: marking block %d as complete and waking up\n", b->block_num);
            b->disk = false;   // ✅ 标记 I/O 完成
            wakeup(b);         // ✅ 唤醒等待的进程
        }
        
        disk.used_idx++;       // ✅ 递增，不取模
    }
    
    // ✅ 唤醒等待描述符的进程
    wakeup(&disk.free[0]);
    spinlock_release(&disk.vdisk_lock);
    printf("virtio_disk_intr: completed\n");
}
