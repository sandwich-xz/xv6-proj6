// SPDX-License-Identifier: BSD-2-Clause
//
// virtio-net 驱动。队列、描述符环、收发逻辑两个架构共用，
// 只在"怎么碰到设备寄存器"这一层分叉：
//   RISC-V  = virtio-mmio（0x10002000）
//   LoongArch = virtio-pci（capability 定位，见 virtio_net_pci.c）
#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "memlayout.h"
#include "virtio.h"
#include "netdev.h"
#include "net.h"

#ifdef ARCH_LOONGARCH
int virtio_net_pci_init(void);
uint8 virtio_net_pci_status(void);
void virtio_net_pci_set_status(uint8);
uint64 virtio_net_pci_features(void);
void virtio_net_pci_set_features(uint64);
void virtio_net_pci_select_queue(uint16);
uint16 virtio_net_pci_queue_max(void);
void virtio_net_pci_set_queue_num(uint16);
void virtio_net_pci_set_queue_addr(uint64, uint64, uint64);
void virtio_net_pci_set_ready(uint16);
void virtio_net_pci_notify(uint16);
uint8 virtio_net_pci_isr(void);
#else
#define R(r) ((volatile uint32 *)(VIRTIO1 + (r)))
#endif

#define VIRTIO_NET_F_MRG_RXBUF 15
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

static uint64
vaddr_dma(void *p)
{
#ifdef ARCH_LOONGARCH
  return ((uint64)p) & ~DMWIN_MASK;
#else
  return (uint64)p;
#endif
}

static uint8
vn_status(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_net_pci_status();
#else
  return *R(VIRTIO_MMIO_STATUS);
#endif
}

static void
vn_set_status(uint8 s)
{
#ifdef ARCH_LOONGARCH
  virtio_net_pci_set_status(s);
#else
  *R(VIRTIO_MMIO_STATUS) = s;
#endif
}

static uint64
vn_features(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_net_pci_features();
#else
  return *R(VIRTIO_MMIO_DEVICE_FEATURES);
#endif
}

static void
vn_set_features(uint64 f)
{
#ifdef ARCH_LOONGARCH
  virtio_net_pci_set_features(f);
#else
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = (uint32)f;
#endif
}

static uint16
vn_queue_max(void)
{
#ifdef ARCH_LOONGARCH
  return virtio_net_pci_queue_max();
#else
  return *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
#endif
}

static void
vn_setup_queue(uint16 q, struct vq *v, uint16 num)
{
#ifdef ARCH_LOONGARCH
  virtio_net_pci_select_queue(q);
  virtio_net_pci_set_queue_num(num);
  virtio_net_pci_set_queue_addr(vaddr_dma(v->desc), vaddr_dma(v->avail),
                                vaddr_dma(v->used));
  virtio_net_pci_set_ready(1);
#else
  *R(VIRTIO_MMIO_QUEUE_SEL) = q;
  *R(VIRTIO_MMIO_QUEUE_NUM) = num;
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = vaddr_dma(v->desc);
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = vaddr_dma(v->desc) >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = vaddr_dma(v->avail);
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = vaddr_dma(v->avail) >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = vaddr_dma(v->used);
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = vaddr_dma(v->used) >> 32;
  *R(VIRTIO_MMIO_QUEUE_READY) = 1;
#endif
  v->num = num;
  v->last_used = 0;
}

static void
vn_notify(uint16 q)
{
#ifdef ARCH_LOONGARCH
  virtio_net_pci_notify(q);
#else
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = q;
#endif
}

static void
rx_post(uint16 id)
{
  rxq.desc[id].addr = vaddr_dma(rxbuf[id]);
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

  txq.desc[0].addr = vaddr_dma(txbuf);
  txq.desc[0].len = len + HDR_LEN;
  txq.desc[0].flags = 0;
  txq.desc[0].next = 0;

  txq.avail->ring[txq.avail->idx % NUM] = 0;
  txq.avail->idx++;
  vn_notify(1);

  for (i = 0; i < 1000000; i++) {
    if (txq.used->idx != txq.last_used) {
      txq.last_used++;
      release(&netlock);
      return 0;
    }
  }
  printf("net: tx timeout used=%d last=%d\n", txq.used->idx, txq.last_used);
  release(&netlock);
  return -1;
}

static void
net_rx_scan(void)
{
  int reposted = 0;

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
    reposted = 1;
  }
  if (reposted)
    vn_notify(0);
}

void
virtio_net_intr(void)
{
#ifndef ARCH_LOONGARCH
  uint32 isr = *R(VIRTIO_MMIO_INTERRUPT_STATUS);
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = isr;
#else
  (void)virtio_net_pci_isr();
#endif
  if (!net_ready)
    return;
  net_rx_scan();
}

// LoongArch 侧暂时用轮询（中断路由尚未接），由时钟中断调用。
void
virtio_net_poll(void)
{
  if (!net_ready)
    return;
  net_rx_scan();
}

void
virtio_net_init(void)
{
  uint32 status = 0;
  uint64 features;
  uint16 qmax;
  int i;

#ifdef ARCH_LOONGARCH
  // TODO(proj6)：设备能识别、两个队列都能 enable（size=8, enable=1）、DRIVER_OK 已置，
  // 但提交的 TX 描述符始终不被消费（used.idx 恒为 0）。下一步要对照块设备驱动逐字段
  // 比较 DMA 地址与 notify 偏移。先不启用，保证 LoongArch 启动干净。
  printf("net: virtio-net-pci 初始化成功，数据通路待修（暂不启用）\n");
  return;
#else
  if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
      *R(VIRTIO_MMIO_VERSION) != 2 || *R(VIRTIO_MMIO_DEVICE_ID) != 1 ||
      *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
    printf("net: no virtio-net at 0x%lx\n", (uint64)VIRTIO1);
    return;
  }
#endif

  initlock(&netlock, "virtio_net");

  vn_set_status(0);
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  vn_set_status(status);
  status |= VIRTIO_CONFIG_S_DRIVER;
  vn_set_status(status);

  features = vn_features();
  features &= ~(1ULL << VIRTIO_NET_F_MRG_RXBUF);
  features &= ~(1ULL << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1ULL << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1ULL << VIRTIO_RING_F_INDIRECT_DESC);
  vn_set_features(features);

  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  vn_set_status(status);
  if (!(vn_status() & VIRTIO_CONFIG_S_FEATURES_OK)) {
    printf("net: FEATURES_OK unset\n");
    return;
  }

  qmax = vn_queue_max();
  if (qmax == 0)
    qmax = NUM;
  if (qmax > NUM)
    qmax = NUM;

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

  vn_setup_queue(0, &rxq, qmax);
  vn_setup_queue(1, &txq, qmax);

#ifdef ARCH_LOONGARCH
  // virtio-net-pci 的 MAC 在设备配置空间；这里用固定值，QEMU 会按 NIC 设置。
  netdev.mac[0] = 0x52;
  netdev.mac[1] = 0x54;
  netdev.mac[2] = 0x00;
  netdev.mac[3] = 0x12;
  netdev.mac[4] = 0x34;
  netdev.mac[5] = 0x57;
#else
  for (i = 0; i < ETH_ALEN; i++)
    netdev.mac[i] = *(volatile uint8 *)((uint64)VIRTIO1 + 0x100 + i);
#endif
  netdev.name[0] = 'v';
  netdev.name[1] = 'i';
  netdev.name[2] = 'r';
  netdev.name[3] = 't';
  netdev.name[4] = '0';
  netdev.name[5] = 0;
  netdev.transmit = net_transmit;

  for (i = 0; i < qmax; i++) {
    rxbuf[i] = (uchar *)kalloc();
    if (rxbuf[i] == 0)
      return;
    rx_post(i);
  }

  net_ready = 1;

  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  vn_set_status(status);
  vn_notify(0);


  if (netdev_register(&netdev) != 0) {
    printf("net: register failed\n");
    net_ready = 0;
  }
}
