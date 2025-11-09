#include "virtio_rng.h"
#include <stdlib.h>
#include "log.h"

RngDev *generate_empty_rngdev(){
    RngDev *rdev = (RngDev *)malloc(sizeof(RngDev));
    rdev->rcv_idx = 0;
    return rdev;
}

int virtio_rng_txq_notify_handler(VirtIODevice* vdev, VirtQueue * vq){
    log_debug("rng i recevied !");
    virtqueue_disable_notify(vq);
    return 0;
}

int virtio_rng_init(VirtIODevice* vdev){
    log_debug("rng init!");
    return 0;
}