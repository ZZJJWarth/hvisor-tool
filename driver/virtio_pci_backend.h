#ifndef __HVISOR_VIRTIO_PCI_BACKEND_H
#define __HVISOR_VIRTIO_PCI_BACKEND_H

#include "hvisor.h"

extern struct virtio_pci_bridge *virtio_pci_bridge;

int hvisor_virtio_pci_alloc_bridge(void);
void hvisor_virtio_pci_free_bridge(void);
int hvisor_virtio_pci_init(void);
void hvisor_virtio_pci_exit(void);

#endif /* __HVISOR_VIRTIO_PCI_BACKEND_H */
