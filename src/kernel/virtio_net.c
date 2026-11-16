// SPDX-License-Identifier: BSD-2-Clause
//
// virtio-net 驱动（先做 RISC-V 的 virtio-mmio 传输）。
// 结构对齐 virtio_disk.c：同样的状态机、同样的队列布局，
// 只是网络要两个队列：0 = 接收，1 = 发送。
#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "memlayout.h"
#include "virtio.h"
#include "netdev.h"
#include "net.h"

#ifdef ARCH_LOONGARCH
// LoongArch 走 virtio-pci，下一阶段补；这里留空保证两个架构都能链接。
void
virtio_net_init(void)
{
}

void
virtio_net_intr(void)
{
}
#else

#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))

#define VIRTIO_NET_F_MRG_RXBUF 15
#define VIRTQ_USED_F_NO_NOTIFY 1

#define RX_BUFSZ 2048
#define HDR_LEN 10 // 不协商 MRG_RXBUF 时 virtio_net_hdr 为 10 字节

struct vq {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  uint16 last_used;
  uint16 num;
};

static struct vq rxq, txq;
static uchar *rxbuf[NUM];
static uchar *txbuf;
static struct netdev netdev;
static int net_ready;
static int seen_frame;
static struct spinlock netlock;

static uint8
vstatus(void)
{
  return *R(VIRTIO_MMIO_STATUS);
}

static void
vset_status(uint8 s)
{
  *R(VIRTIO_MMIO_STATUS) = s;
}

static void
vnotify(uint16 q)
{
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = q;
}

static void
vqueue_setup(uint16 q, struct vq *v, uint16 num)
{
  *R(VIRTIO_MMIO_QUEUE_SEL) = q;
  *R(VIRTIO_MMIO_QUEUE_NUM) = num;
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)v->desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)v->desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)v->avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)v->avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)v->used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)v->used >> 32;
  *R(VIRTIO_MMIO_QUEUE_READY) = 1;
  v->num = num;
  v->last_used = 0;
}

static void
rx_post(uint16 id)
{
  rxq.desc[id].addr = (uint64)rxbuf[id];
  rxq.desc[id].len = RX_BUFSZ;
  rxq.desc[id].flags = VRING_DESC_F_WRITE;
  rxq.desc[id].next = 0;
  rxq.avail->ring[rxq.avail->idx % NUM] = id;
  rxq.avail->idx++;
}

static int
net_transmit(struct netdev *dev, uchar *pkt, uint len)
{
  int i;

  (void)dev;
  if (len > ETH_MTU)
    return -1;

  acquire(&netlock);
  memset(txbuf, 0, HDR_LEN);
  memmove(txbuf + HDR_LEN, pkt, len);

  txq.desc[0].addr = (uint64)txbuf;
  txq.desc[0].len = len + HDR_LEN;
  txq.desc[0].flags = 0;
  txq.desc[0].next = 0;

  txq.avail->ring[txq.avail->idx % NUM] = 0;
  txq.avail->idx++;
  vnotify(1);

  for (i = 0; i < 1000000; i++) {
    if (txq.used->idx != txq.last_used) {
      txq.last_used++;
      release(&netlock);
      return 0;
    }
  }
  release(&netlock);
  return -1;
}

void
virtio_net_intr(void)
{
  uint32 isr = *R(VIRTIO_MMIO_INTERRUPT_STATUS);
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = isr;

  if (!net_ready)
    return;

  while (rxq.used->idx != rxq.last_used) {
    struct virtq_used_elem e = rxq.used->ring[rxq.last_used % NUM];
    uint16 id = e.id;
    uint32 len = e.len;

    rxq.last_used++;

    if (len > HDR_LEN && id < NUM) {
      if (!seen_frame) {
        seen_frame = 1;
        printf("net: first frame %d bytes\n", len - HDR_LEN);
      }
      netdev_input(rxbuf[id] + HDR_LEN, len - HDR_LEN);
    }
    rx_post(id);
  }
  vnotify(0);
}

void
virtio_net_init(void)
{
  uint32 status = 0;
  uint64 features;
  int i;

  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 2 || *R(VIRTIO_MMIO_DEVICE_ID) != 1 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    printf("net: no virtio-net at 0x%lx\n", (uint64)VIRTIO1);
    return;
  }

  initlock(&netlock, "virtio_net");

  vset_status(0);
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  vset_status(status);
  status |= VIRTIO_CONFIG_S_DRIVER;
  vset_status(status);

  features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1ULL << VIRTIO_NET_F_MRG_RXBUF);
  features &= ~(1ULL << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = (uint32)features;

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  vset_status(status);
  if (!(vstatus() & VIRTIO_CONFIG_S_FEATURES_OK)) {
    printf("net: FEATURES_OK unset\n");
    return;
  }

  rxq.desc = (struct virtq_desc *)kalloc();
  rxq.avail = (struct virtq_avail *)kalloc();
  rxq.used = (struct virtq_used *)kalloc();
  txq.desc = (struct virtq_desc *)kalloc();
  txq.avail = (struct virtq_avail *)kalloc();
  txq.used = (struct virtq_used *)kalloc();
  txbuf = (uchar *)kalloc();
  if (rxq.desc == 0 || rxq.avail == 0 || rxq.used == 0 || txq.desc == 0 ||
      txq.avail == 0 || txq.used == 0 || txbuf == 0) {
    printf("net: out of memory\n");
    return;
  }
  memset(rxq.desc, 0, PGSIZE);
  memset(rxq.avail, 0, PGSIZE);
  memset(rxq.used, 0, PGSIZE);
  memset(txq.desc, 0, PGSIZE);
  memset(txq.avail, 0, PGSIZE);
  memset(txq.used, 0, PGSIZE);

  vqueue_setup(0, &rxq, NUM);
  vqueue_setup(1, &txq, NUM);

  // MAC 在设备配置空间 0x100 起
  for (i = 0; i < ETH_ALEN; i++)
    netdev.mac[i] = *(volatile uint8 *)((uint64)VIRTIO1 + 0x100 + i);
  netdev.name[0] = 'v';
  netdev.name[1] = 'i';
  netdev.name[2] = 'r';
  netdev.name[3] = 't';
  netdev.name[4] = '0';
  netdev.name[5] = 0;
  netdev.transmit = net_transmit;

  for (i = 0; i < NUM; i++) {
    rxbuf[i] = (uchar *)kalloc();
    if (rxbuf[i] == 0)
      return;
    rx_post(i);
  }

  net_ready = 1;
  vnotify(0);

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  vset_status(status);

  if (netdev_register(&netdev) != 0) {
    printf("net: register failed\n");
    net_ready = 0;
  }
}

#endif
