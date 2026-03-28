#include "virtio_rng.h"
#include <stdlib.h>
#include "log.h"
#include <unistd.h>
#include <fcntl.h>

RngDev* generate_empty_rngdev(){
    RngDev *rdev = (RngDev *)malloc(sizeof(RngDev));
    rdev->rcv_idx = 0;
    return rdev;
}

int virtio_rng_txq_notify_handler(VirtIODevice* vdev, VirtQueue * vq){
    log_info("rng i recevied !");
    virtqueue_disable_notify(vq);   
    int fd = open("/dev/hwrng",O_RDONLY);
    log_info("rng i opened !");
    if(fd < 0){
        perror("open");
        virtqueue_enable_notify(vq);
        return -1;
    }
    uint16_t desc_idx,*flags;
    struct iovec *iov;
    int n = process_descriptor_chain(vq,&desc_idx,&iov,&flags,0,1);
    log_info("rng i process");
    int total = 0;
    for(int i = 0;i<n;i++){
        void *bufferp = iov[i].iov_base;
        size_t bufferl = iov[i].iov_len;
        while(bufferl>0){
            ssize_t rn = read(fd,bufferp,bufferl);
            if(rn <= 0){
                perror("open");
                virtqueue_enable_notify(vq);
                return -1;
            }
            bufferp += rn;
            bufferl -= rn;
            total += rn;
        }
    }
    close(fd);
    // size_t total = 0;
    // ssize_t = read()
    update_used_ring(vq,desc_idx,total);
    virtqueue_enable_notify(vq);
    log_info("rng i close!");
    virtio_inject_irq(vq);
    log_info("rng_i_irq");
    return 0;
}

int virtio_rng_init(VirtIODevice* vdev){
    log_info("rng init!");
    vdev->regs.device_id = 4;
    return 0;
}