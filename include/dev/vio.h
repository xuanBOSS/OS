#ifndef __VIO_H__
#define __VIO_H__

#include "common.h"

typedef struct buf buf_t;

void virtio_disk_init(void);
void virtio_disk_intr(void);
void virtio_disk_rw(buf_t *b, bool write);

#endif
