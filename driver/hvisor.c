// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) 2025 Syswonder
 *
 * Syswonder Website:
 *      https://www.syswonder.org
 *
 * Authors:
 *      Guowei Li <2401213322@stu.pku.edu.cn>
 */
#include <asm/cacheflush.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_reserved_mem.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/random.h>

#include "hvisor.h"
#include "zone_config.h"

struct virtio_bridge *virtio_bridge;
struct virtio_pci_bridge *virtio_pci_bridge;
int virtio_irq = -1;
int virtio_pci_irq_config = -1;
int virtio_pci_irq_data = -1;
static struct task_struct *task = NULL;
static struct virtio_pci_dev virtpci_dev_list[MAX_VIRTPCI_DEV];
static int nxt_dev_idx = 0;

// initial virtio el2 shared region
static int hvisor_init_virtio(void) {
    int err;
    if (virtio_irq == -1) {
        pr_err("virtio device is not available\n");
        return ENOTTY;
    }
    virtio_bridge = (struct virtio_bridge *)__get_free_pages(GFP_KERNEL, 0);
    virtio_pci_bridge = (struct virtio_pci_bridge *)__get_free_pages(GFP_KERNEL, 0);
    if (virtio_bridge == NULL)
        return -ENOMEM;
    if (virtio_pci_bridge == NULL)
        return -ENOMEM;
    SetPageReserved(virt_to_page(virtio_bridge));
    SetPageReserved(virt_to_page(virtio_pci_bridge));
    // init device region
    memset(virtio_bridge, 0, sizeof(struct virtio_bridge));
    memset(virtio_pci_bridge, 0, sizeof(struct virtio_pci_bridge));
    err = hvisor_call(HVISOR_HC_INIT_VIRTIO, __pa(virtio_bridge), __pa(virtio_pci_bridge));
    if (err)
        return err;
    pr_info("virtio init complete");
    return 0;
}

// finish virtio req and send result to el2
static int hvisor_finish_req(void) {
    int err;
    err = hvisor_call(HVISOR_HC_FINISH_REQ, 0, 0);
    if (err)
        return err;
    return 0;
}

// static int flush_cache(__u64 phys_start, __u64 size)
// {
//     void __iomem *vaddr;
//     int err = 0;

//     size = PAGE_ALIGN(size);

//     // 使用 ioremap 映射物理地址
//     vaddr = ioremap_cache(phys_start, size);
//     if (!vaddr) {
//         pr_err("hvisor.ko: failed to ioremap image\n");
//         return -ENOMEM;
//     }

//     // flush I-cache（ARM64 平台中 flush_icache_range 是对 D/I 的处理）
//     flush_icache_range((unsigned long)vaddr, (unsigned long)vaddr + size);

//     // 解除映射
//     iounmap(vaddr);
//     return err;
// }
// static int flush_cache(__u64 phys_start, __u64 size)
// {
//     struct vm_struct *vma;
//     int err = 0;
//     size = PAGE_ALIGN(size);
//     vma = get_vm_area(size, VM_IOREMAP);
//     if (!vma)
//     {
//         pr_err("hvisor.ko: failed to allocate virtual kernel memory for
//         image\n"); return -ENOMEM;
//     }
//     vma->phys_addr = phys_start;

//     if (ioremap_page_range((unsigned long)vma->addr, (unsigned
//     long)(vma->addr + size), phys_start, PAGE_KERNEL_EXEC))
//     {
//         pr_err("hvisor.ko: failed to ioremap image\n");
//         err = -EFAULT;
//         goto unmap_vma;
//     }
//     // flush icache will also flush dcache
//     flush_icache_range((unsigned long)(vma->addr), (unsigned long)(vma->addr
//     + size));

// unmap_vma:
//     vunmap(vma->addr);
//     return err;
// }

static int hvisor_zone_start(zone_config_t __user *arg) {
    int err = 0;
    zone_config_t *zone_config = kmalloc(sizeof(zone_config_t), GFP_KERNEL);

    if (zone_config == NULL) {
        pr_err("hvisor.ko: failed to allocate memory for zone_config\n");
    }

    if (copy_from_user(zone_config, arg, sizeof(zone_config_t))) {
        pr_err("hvisor.ko: failed to copy from user\n");
        kfree(zone_config);
        return -EFAULT;
    }

    // flush_cache(zone_config->kernel_load_paddr, zone_config->kernel_size);
    // flush_cache(zone_config->dtb_load_paddr, zone_config->dtb_size);

    pr_info("hvisor.ko: invoking hypercall to start the zone\n");

    err = hvisor_call(HVISOR_HC_START_ZONE, __pa(zone_config),
                      sizeof(zone_config_t));
    kfree(zone_config);
    return err;
}

// #ifndef LOONGARCH64
// static int is_reserved_memory(unsigned long phys, unsigned long size) {
//     struct device_node *parent, *child;
//     struct reserved_mem *rmem;
//     phys_addr_t mem_base;
//     size_t mem_size;
//     int count = 0;
//     parent = of_find_node_by_path("/reserved-memory");
//     count = of_get_child_count(parent);

//     for_each_child_of_node(parent, child) {
//         rmem = of_reserved_mem_lookup(child);
//         mem_base = rmem->base;
//         mem_size = rmem->size;
//         if (mem_base <= phys && (mem_base + mem_size) >= (phys + size)) {
//             return 1;
//         }
//     }
//     return 0;
// }
// #endif

static int hvisor_config_check(u64 __user *arg) {
    int err = 0;
    u64 *config;
    config = kmalloc(sizeof(u64), GFP_KERNEL);
    err = hvisor_call(HVISOR_HC_CONFIG_CHECK, __pa(config), 0);

    if (err != 0) {
        pr_err("hvisor.ko: failed to get hvisor config\n");
    }

    if (copy_to_user(arg, config, sizeof(u64))) {
        pr_err("hvisor.ko: failed to copy to user\n");
        kfree(config);
        return -EFAULT;
    }

    kfree(config);
    return err;
}

static int hvisor_zone_list(zone_list_args_t __user *arg) {
    int ret;
    zone_info_t *zones;
    zone_list_args_t args;

    /* Copy user provided arguments to kernel space */
    if (copy_from_user(&args, arg, sizeof(zone_list_args_t))) {
        pr_err("hvisor.ko: failed to copy from user\n");
        return -EFAULT;
    }

    zones = kmalloc(args.cnt * sizeof(zone_info_t), GFP_KERNEL);
    memset(zones, 0, args.cnt * sizeof(zone_info_t));

    ret = hvisor_call(HVISOR_HC_ZONE_LIST, __pa(zones), args.cnt);
    if (ret < 0) {
        pr_err("hvisor.ko: failed to get zone list\n");
        goto out;
    }
    // copy result back to user space
    if (copy_to_user(args.zones, zones, ret * sizeof(zone_info_t))) {
        pr_err("hvisor.ko: failed to copy to user\n");
        goto out;
    }
out:
    kfree(zones);
    return ret;
}

static long hvisor_ioctl(struct file *file, unsigned int ioctl,
                         unsigned long arg) {
    int err = 0;
    switch (ioctl) {
    case HVISOR_INIT_VIRTIO:
        err = hvisor_init_virtio();
        task = get_current(); // get hvisor user process
        break;
    case HVISOR_ZONE_START:
        err = hvisor_zone_start((zone_config_t __user *)arg);
        break;
    case HVISOR_ZONE_SHUTDOWN:
        err = hvisor_call(HVISOR_HC_SHUTDOWN_ZONE, arg, 0);
        break;
    case HVISOR_ZONE_LIST:
        err = hvisor_zone_list((zone_list_args_t __user *)arg);
        break;
    case HVISOR_FINISH_REQ:
        err = hvisor_finish_req();
        break;
    case HVISOR_CONFIG_CHECK:
        err = hvisor_config_check((u64 __user *)arg);
        break;
#ifdef LOONGARCH64
    case HVISOR_CLEAR_INJECT_IRQ:
        err = hvisor_call(HVISOR_HC_CLEAR_INJECT_IRQ, 0, 0);
        break;
#endif
    default:
        err = -EINVAL;
        break;
    }
    return err;
}

// Kernel mmap handler
static int hvisor_map(struct file *filp, struct vm_area_struct *vma) {
    unsigned long phys;
    int err;
    if (vma->vm_pgoff == 0) {
        // virtio_bridge must be aligned to one page.
        phys = virt_to_phys(virtio_bridge);
        // vma->vm_flags |= (VM_IO | VM_LOCKED | (VM_DONTEXPAND | VM_DONTDUMP));
        // Not sure should we add this line.
        err = remap_pfn_range(vma, vma->vm_start, phys >> PAGE_SHIFT,
                              vma->vm_end - vma->vm_start, vma->vm_page_prot);
        if (err)
            return err;
        pr_info("virtio bridge mmap succeed!\n");
    } else {
        size_t size = vma->vm_end - vma->vm_start;
        // TODO: add check for non root memory region.
        // memremap(0x50000000, 0x30000000, MEMREMAP_WB);
        // vm_pgoff is the physical page number.
        // if (!is_reserved_memory(vma->vm_pgoff << PAGE_SHIFT, size)) {
        //     pr_err("The physical address to be mapped is not within the
        //     reserved memory\n"); return -EFAULT;
        // }
        err = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, size,
                              vma->vm_page_prot);
        if (err)
            return err;
        pr_info("non root region mmap succeed!\n");
    }
    return 0;
}

static const struct file_operations hvisor_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = hvisor_ioctl,
    .compat_ioctl = hvisor_ioctl,
    .mmap = hvisor_map,
};

static struct miscdevice hvisor_misc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "hvisor",
    .fops = &hvisor_fops,
};



// Interrupt handler for Virtio device.
static irqreturn_t virtio_irq_handler(int irq, void *dev_id) {
    struct siginfo info;
    if (dev_id != &hvisor_misc_dev) {
        return IRQ_NONE;
    }

    memset(&info, 0, sizeof(struct siginfo));
    info.si_signo = SIGHVI;
    info.si_code = SI_QUEUE;
    info.si_int = 1;
    // Send signal SIGHVI to hvisor user task
    if (task != NULL) {
        // pr_info("send signal to hvisor device\n");
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(4, 20, 0))
        if (send_sig_info(SIGHVI, (struct siginfo *)&info, task) < 0) {
            pr_err("Unable to send signal\n");
        }
#else
        if (send_sig_info(SIGHVI, (struct kernel_siginfo *)&info, task) < 0) {
            pr_err("Unable to send signal\n");
        }
#endif
    }
    return IRQ_HANDLED;
}

static irqreturn_t virtio_pci_irq_init_handler(int irq, void *dev_id){
    struct virtqueue_info test = virtio_pci_bridge->req_list[1];
    pr_info("114514: I get pci req: desc:0x%x avail:0x%x used:0x%x\n",test.desc_area,test.avail_area,test.used_area);
    return IRQ_WAKE_THREAD;
}

struct virtqueue_info temp;

// Interrupt handler for Virtio device.
static irqreturn_t virtio_pci_irq_handler(int irq, void *dev_id) {
    pr_info("pci irq thread!\n");
    struct virtqueue_info config = virtio_pci_bridge->req_list[0];
    __u64 index = config.desc_area;
    struct virtqueue_info target = virtio_pci_bridge->req_list[index];
    if (config.avail_area == 0){
        
        void *addr = memremap(target.desc_area,0x2000,MEMREMAP_WB);
        temp.desc_area = addr;
        temp.avail_area = addr + (target.avail_area - target.desc_area);
        temp.used_area = addr + (target.used_area - target.desc_area);
        if(!addr){
            pr_info("addr memremap failed!\n");
        }else{
            pr_info("addr memremap success\n");
        }
        return IRQ_HANDLED;
    }else{
        struct virtq_desc *desc = temp.desc_area;
        struct virtq_avail *avail = temp.avail_area;
        struct virtq_used *used = temp.used_area;
        int avail_idx = avail->idx;
        int used_idx = used->idx;
        {
            int i = used_idx;
            while(i<avail_idx){
                int j = i%256;
                int avail_ring_content = avail->ring[j];
                struct virtq_desc desc_item = desc[avail_ring_content];
                struct virtq_used_elem uitem;
                void* buffer = memremap(desc_item.addr,desc_item.len,MEMREMAP_WB);
                if(buffer == NULL){
                    pr_err("mem err!");
                }else{
                    memset(buffer,'Z',desc_item.len);
                    get_random_bytes(buffer, desc_item.len);
                    memunmap(buffer);
                }
                uitem.id = avail_ring_content;
                uitem.len = desc_item.len;
                used->ring[j] = uitem;
                i++;
            }
            used->idx = i;
        }
        
        pr_info("avail_idx = %d,used_idx = %d\n",avail_idx,used_idx);
        return IRQ_HANDLED;
    }
    // void *addr;
    // addr = memremap(test.used_area,0x1000,MEMREMAP_WB);
    // if(!addr){
    //     pr_info("addr memremap failed!");
    // }
    // else{
    //     *(__u64 *) addr = 1;
    // }

    return IRQ_HANDLED;
}

static int create_virtio_pci_dev(__u16 num_of_vq,struct virtqueue_info* vqs,__u16 features,void (*handler)(struct virtqueue_info*,int q_id)){
    
    if(nxt_dev_idx >= MAX_VIRTPCI_DEV){
        pr_err("nxt_dev_idx >= MAX_VIRTPCI_DEV there are too much device!\n");
        return 0;
    }
    if(num_of_vq > MAX_VQ){
        pr_err("num_of_vq > MAX_VQ there are too much virtqueu in the virtio device\n");
        return 0;
    }
    struct virtio_pci_dev* birth = &virtpci_dev_list[nxt_dev_idx];
    birth->num_of_vq = num_of_vq;
    int i = 0;
    while(i < num_of_vq){
        // void *addr = memremap(desc_area,0x2000);
        void *addr = memremap(vqs[i].desc_area,0x2000,MEMREMAP_WB);
        if(!addr){
            pr_info("addr memremap failed!\n");
            return IRQ_HANDLED;
        }else{
            pr_info("addr memremap success\n");
        }
        birth->vqs[i].desc_area = addr;
        birth->vqs[i].avail_area = addr + (vqs[i].avail_area - vqs[i].desc_area);
        birth->vqs[i].used_area = addr + (vqs[i].used_area - vqs[i].desc_area);
        pr_info("desc_area:0x%x,avail_area:0x%x,used_area:0x%x\n",birth->vqs[i],birth->vqs[i].avail_area,birth->vqs[i].used_area);
        i++;
    }

    birth->features = features;
    birth->data_req_handler = handler;
    return nxt_dev_idx++;
}

static void virtio_rng_handler(struct virtqueue_info *vq,int queue_id){
    struct virtq_desc *desc = vq->desc_area;
    struct virtq_avail *avail = vq->avail_area;
    struct virtq_used *used = vq->used_area;
    if(desc == NULL || avail == NULL || used == NULL){
        pr_err("desc :0x%x,avail:0x%x,used:0x%x\n");
        return IRQ_HANDLED;
    }
    int avail_idx = avail->idx;
    int used_idx = used->idx;
    pr_info("avail_idx:%d,use_idx:%d\n",avail_idx,used_idx);
    {
        int i = used_idx;
        while(i<avail->idx){
            int j = i%256;
            int avail_ring_content = avail->ring[j];
            struct virtq_desc desc_item = desc[avail_ring_content];
            struct virtq_used_elem uitem;
            void* buffer = memremap(desc_item.addr,desc_item.len,MEMREMAP_WB);
            pr_info("buffer:%x\n",buffer);
            if(buffer == NULL){
                pr_err("mem err!");
            }else{
                // memset(buffer,'Z',desc_item.len);
                get_random_bytes(buffer, desc_item.len);
                memunmap(buffer);
            }
            uitem.id = avail_ring_content;
            uitem.len = desc_item.len;
            used->ring[j] = uitem;
            i++;
        }
        used->idx = i;
    }
    
    pr_info("avail_idx = %d,used_idx = %d\n",avail->idx,used->idx);
    return IRQ_HANDLED;
}

static irqreturn_t virtio_pci_irq_config_handler(int irq, void *dev_id){
    return IRQ_WAKE_THREAD;
}

static irqreturn_t virtio_pci_irq_config_thread_handler(int irq,void *dev_id){
    if(virtio_pci_bridge == NULL){
        pr_err("virtio_pci_bridge has not been initialized!");
        return IRQ_HANDLED;
    }
    void (*handler)(struct virtqueue_info*,int ) = NULL;
    struct virtio_pci_config_info *info = &virtio_pci_bridge->config;
    switch(info->dtype){
        case VIRTIO_PCI_RNG:
            handler = virtio_rng_handler;
            break;
        default:
            break;
    }
    info->dev_id = create_virtio_pci_dev(info->num_of_queues,info->vqs,info->features,handler);
     
    return IRQ_HANDLED;
}

static irqreturn_t virtio_pci_irq_data_handler(int irq,void *dev_id){
    return IRQ_WAKE_THREAD;
}

static irqreturn_t virtio_pci_irq_data_thread_handler(int irq,void *dev_id){
    if(virtio_pci_bridge == NULL){
        pr_err("virtio_pci_bridge has not been initialized!\n");
        return IRQ_HANDLED;
    }

    struct virtio_pci_data_info *info = &virtio_pci_bridge->data;
    __u64 dev_idx = info->dev_id;
    __u64 queue_id = info->queue_id;
    __u64 cpu_id = info->cpu_id;
    pr_info("dev_id:0x%x,queue_id:0x%x,cpu_id:0x%x\n",dev_idx,queue_id,cpu_id);
    __u64 data_req_id = dev_idx | (queue_id << 16) | (cpu_id<<32);
    if(dev_idx>=nxt_dev_idx){
        pr_err("the dev_idx given by hvisor is invaild!\n");
        return IRQ_HANDLED;
    }
    

    struct virtio_pci_dev *dev = &virtpci_dev_list[dev_idx];
    if(queue_id > dev->num_of_vq){
        pr_err("the queue_id given by hvisor is invaild!\n");
        return IRQ_HANDLED;
    }
    struct virtqueue_info *vq = &dev->vqs[queue_id];

    if(dev->data_req_handler != NULL){
        dev->data_req_handler(vq,queue_id);
    }
    pr_info("the data_req_id is %x\n",data_req_id);
    hvisor_call(HVISOR_HC_VIRTIO_PCI_DONE,data_req_id,0);
    
    return IRQ_HANDLED;
}

// #undef X86_64

/*
** Module Init function
*/
static int __init hvisor_init(void) {
    int err;
    struct device_node *node = NULL,*virtio_pci_node_config = NULL,*virtio_pci_node_data = NULL;
    u32 *irq;
    err = misc_register(&hvisor_misc_dev);
    if (err) {
        pr_err("hvisor_misc_register failed!!!\n");
        return err;
    }
#ifndef X86_64
    // probe hvisor virtio device.
    // The irq number must be retrieved from dtb node, because it is different
    // from GIC's IRQ number.
    node = of_find_node_by_path("/hvisor_virtio_device");
    virtio_pci_node_config = of_find_node_by_path("/hvisor_virtio_pci_config");
    virtio_pci_node_data = of_find_node_by_path("/hvisor_virtio_pci_data");
    
    if (!node || !virtio_pci_node_config || !virtio_pci_node_data) {
        pr_err("Critical: Missing device tree node!\n");
        pr_err("   Please add the following to your device tree:\n");
        pr_err("   hvisor_virtio_device {\n");
        pr_err("       compatible = \"hvisor\";\n");
        pr_err("       interrupts = <0x00 0x20 0x01>;\n");
        pr_err("   };\n");
        return -ENODEV;
    }
    
    virtio_pci_irq_config = of_irq_get(virtio_pci_node_config,0);
    virtio_irq = of_irq_get(node, 0);
    virtio_pci_irq_data = of_irq_get(virtio_pci_node_data,0);
    pr_info("virtio_irq = %d\n", virtio_irq);
    pr_info("virtio_pci_irq = %d\n", virtio_pci_irq_config);
    pr_info("virtio_pci_irq_data = %d\n",virtio_pci_irq_data);
    err = request_irq(virtio_irq, virtio_irq_handler,
                      IRQF_SHARED | IRQF_TRIGGER_RISING, "hvisor_virtio_device",
                      &hvisor_misc_dev);
    if (err)
        goto err_out;
    // err = request_irq(virtio_pci_irq,virtio_pci_irq_handler,
    //                   0 , "hvisor_virtio_pci",NULL);
    // err = request_threaded_irq(virtio_pci_irq_config,
    //                        virtio_pci_irq_init_handler,
    //                        virtio_pci_irq_handler,
    //                        0,
    //                        "hvisor_virtio_pci",
    //                        NULL);
    err = request_threaded_irq(virtio_pci_irq_config,
                            virtio_pci_irq_config_handler,
                            virtio_pci_irq_config_thread_handler,
                            0,
                            "hvisor_virtio_pci_config",
                            NULL);
    pr_info("err is :%d\n",err);
    err = request_threaded_irq(virtio_pci_irq_data,
                            virtio_pci_irq_data_handler,
                            virtio_pci_irq_data_thread_handler,
                            0,
                            "hvisor_virtio_pci_data",
                            NULL);
    pr_info("err is :%d\n",err);


    of_node_put(virtio_pci_node_config);
    of_node_put(virtio_pci_node_data);
    of_node_put(node);
#else
    // we don't use device tree in x86_64, so we have to get IRQ using hypercall
    irq = kmalloc(sizeof(u32), GFP_KERNEL);
    err = hvisor_call(HVISOR_HC_GET_VIRTIO_IRQ, __pa(irq), 0);
    virtio_irq = *irq;
    err = request_irq(virtio_irq, virtio_irq_handler, IRQF_SHARED,
                      "hvisor_virtio_device", &hvisor_misc_dev);
    if (err)
        goto err_out;

    kfree(irq);
#endif /* X86_64 */
    pr_info("hvisor init done!!!\n");
    return 0;
err_out:
    pr_err("hvisor cannot register IRQ, err is %d\n", err);
    if (virtio_irq != -1)
        free_irq(virtio_irq, &hvisor_misc_dev);
    misc_deregister(&hvisor_misc_dev);
    return err;
}

/*
** Module Exit function
*/
static void __exit hvisor_exit(void) {
    if (virtio_irq != -1)
        free_irq(virtio_irq, &hvisor_misc_dev);
    if (virtio_bridge != NULL) {
        ClearPageReserved(virt_to_page(virtio_bridge));
        free_pages((unsigned long)virtio_bridge, 0);
    }
    misc_deregister(&hvisor_misc_dev);
    pr_info("hvisor exit!!!\n");
}

module_init(hvisor_init);
module_exit(hvisor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KouweiLee <15035660024@163.com>");
MODULE_DESCRIPTION("The hvisor device driver");
MODULE_VERSION("1:0.0");