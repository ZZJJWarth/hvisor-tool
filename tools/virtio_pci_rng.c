#include <errno.h>
#include <sys/random.h>

#include "virtio_pci_userspace.h"
#include "virtio_rng.h"

static int fill_random(void *buf, size_t len) {
    char *ptr = buf;

    while (len > 0) {
        ssize_t ret = getrandom(ptr, len, 0);

        if (ret <= 0)
            return -1;
        ptr += ret;
        len -= (size_t)ret;
    }
    return 0;
}

int userspace_pci_rng_init_dev(UserspacePciDev *dev,
                               const struct virtio_pci_config_info *info) {
    (void)dev;
    (void)info;
    return 0;
}

void userspace_pci_rng_cleanup_dev(UserspacePciDev *dev) { (void)dev; }

int userspace_pci_rng_process_vq(UserspacePciVirtQueue *vq) {
    __u16 used_idx;

    if (vq == NULL || vq->desc == NULL || vq->avail == NULL || vq->used == NULL)
        return -EINVAL;
    if (vq->queue_size == 0)
        return -EINVAL;

    used_idx = vq->used->idx;
    while (used_idx != vq->avail->idx) {
        __u16 slot = used_idx % vq->queue_size;
        __u16 head = vq->avail->ring[slot];
        __u16 idx = head;
        __u32 total_len = 0;
        __u16 seen = 0;

        if (head >= vq->queue_size)
            return -EIO;

        for (;;) {
            volatile struct virtq_desc *desc;
            void *buf;

            if (seen++ >= vq->queue_size)
                return -EIO;
            if (idx >= vq->queue_size)
                return -EIO;

            desc = &vq->desc[idx];
            if ((desc->flags & VRING_DESC_F_WRITE) == 0)
                return -EINVAL;
            if (userspace_pci_translate_addr(desc->addr, desc->len, &buf) != 0)
                return -EIO;
            if (fill_random(buf, desc->len) != 0) {
                return -EIO;
            }
            total_len += desc->len;

            if ((desc->flags & VRING_DESC_F_NEXT) == 0)
                break;
            idx = desc->next;
        }

        vq->used->ring[slot].id = head;
        vq->used->ring[slot].len = total_len;
        __sync_synchronize();
        used_idx++;
        vq->used->idx = used_idx;
    }

    return 0;
}
