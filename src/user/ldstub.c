// SPDX-License-Identifier: BSD-2-Clause
//
// 动态链接器：内核把主程序与解释器都映射好、把 auxv 压在栈上，我们从这里接手。
// 干三件事：按 DT_NEEDED 把共享库映射进来、应用库自身的 RELATIVE 重定位、
// 把主程序的 JUMP_SLOT 填成真实地址，最后跳到 AT_ENTRY。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define SHT_DYNSYM 11

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_JMPREL 23

#define R_RISCV_RELATIVE 3
#define R_RISCV_JUMP_SLOT 5

#define AT_NULL 0
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_ENTRY 9

#define MAXLIBS 4

struct ehdr {
  uchar ident[16];
  ushort type, machine;
  uint version;
  uint64 entry, phoff, shoff;
  uint flags;
  ushort ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct phdr {
  uint type, flags;
  uint64 offset, vaddr, paddr, filesz, memsz, align;
};

struct shdr {
  uint name, type;
  uint64 flags, addr, offset, size;
  uint link, info;
  uint64 addralign, entsize;
};

struct sym {
  uint name;
  uchar info, other;
  ushort shndx;
  uint64 value, size;
};

struct rela {
  uint64 offset, info;
  long long addend;
};

struct lib {
  uchar *buf;
  int len;
  uint64 base;
};

static uchar mbuf[65536];
static int mlen;
static uchar libbufs[MAXLIBS][65536];
static struct lib libs[MAXLIBS];
static int nlibs;

static int
read_file(char *path, uchar *buf, int cap)
{
  int fd, n, tot = 0;

  fd = open(path, 0);
  if (fd < 0)
    return -1;
  while ((n = read(fd, buf + tot, cap - tot)) > 0) {
    tot += n;
    if (tot == cap)
      break;
  }
  close(fd);
  return tot;
}

static struct sym *
find_symbol(struct lib *l, char *name, uint64 *addr)
{
  struct ehdr *e = (struct ehdr *)l->buf;
  int i;

  for (i = 0; i < e->shnum; i++) {
    struct shdr *sh = (struct shdr *)(l->buf + e->shoff + i * e->shentsize);
    struct sym *s;
    uint64 cnt;
    char *str;

    if (sh->type != SHT_DYNSYM)
      continue;
    s = (struct sym *)(l->base + sh->addr);
    cnt = sh->size / sizeof(struct sym);
    str = (char *)(l->base +
                   ((struct shdr *)(l->buf + e->shoff + sh->link * e->shentsize))->addr);
    for (uint64 k = 0; k < cnt; k++) {
      if (s[k].name && strcmp(str + s[k].name, name) == 0) {
        *addr = l->base + s[k].value;
        return s;
      }
    }
  }
  return 0;
}

static uint64
map_lib(char *path, int idx)
{
  struct ehdr *e;
  struct phdr *ph;
  uchar *buf = mbuf; // DEBUG: reuse the buffer that worked for the main ELF
  uint64 end = 0, base;
  int i, n;

  n = read_file(path, buf, sizeof(libbufs[idx]));
  if (n <= 0) {
    printf("ld.so: cannot read %s\n", path);
    return 0;
  }
  e = (struct ehdr *)buf;
  if (e->ident[0] != 0x7f || e->ident[1] != 'E' || e->ident[2] != 'L' ||
      e->ident[3] != 'F' || e->type != 3)
    return 0;

  for (i = 0; i < e->phnum; i++) {
    ph = (struct phdr *)(buf + e->phoff + i * e->phentsize);
    if (ph->type == PT_LOAD && ph->vaddr + ph->memsz > end)
      end = ph->vaddr + ph->memsz;
  }
  if (end == 0)
    return 0;

  base = mmap(0, (int)end);
  if (base == (uint64)-1)
    return 0;

  for (i = 0; i < e->phnum; i++) {
    ph = (struct phdr *)(buf + e->phoff + i * e->phentsize);
    if (ph->type != PT_LOAD)
      continue;
    memmove((void *)(base + ph->vaddr), buf + ph->offset, ph->filesz);
    if (ph->memsz > ph->filesz)
      memset((void *)(base + ph->vaddr + ph->filesz), 0,
             ph->memsz - ph->filesz);
  }

  libs[nlibs].buf = buf;
  libs[nlibs].len = n;
  libs[nlibs].base = base;
  nlibs++;

  // 库自身的 RELATIVE 重定位
  for (i = 0; i < e->phnum; i++) {
    ph = (struct phdr *)(buf + e->phoff + i * e->phentsize);
    if (ph->type != PT_DYNAMIC)
      continue;
    {
      uint64 *d = (uint64 *)(base + ph->vaddr);
      uint64 rela = 0, relasz = 0;

      for (; d[0] != DT_NULL; d += 2) {
        if (d[0] == DT_RELA)
          rela = d[1];
        else if (d[0] == DT_RELASZ)
          relasz = d[1];
      }
      for (uint64 k = 0; k < relasz / sizeof(struct rela); k++) {
        struct rela *r = (struct rela *)(base + rela) + k;
        if ((r->info & 0xffffffff) == R_RISCV_RELATIVE)
          *(uint64 *)(base + r->offset) = base + r->addend;
      }
    }
  }
  return base;
}

static uint64
lookup(char *name)
{
  uint64 addr = 0;
  int i;

  for (i = 0; i < nlibs; i++) {
    if (find_symbol(&libs[i], name, &addr))
      return addr;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  uint64 *auxv = (uint64 *)(argv + argc + 1);
  uint64 entry = 0;
  struct ehdr *e;
  char path[128];
  int i, n;

  for (i = 0; auxv[i] != AT_NULL && i < 16; i += 2)
    if (auxv[i] == AT_ENTRY)
      entry = auxv[i + 1];

  if (entry == 0 || argc < 1) {
    printf("ld.so: bad auxv\n");
    exit(1);
  }

  // 读主程序的 ELF（argv[0] 可能不带 '/'，退回到根目录）
  path[0] = 0;
  n = read_file(argv[0], mbuf, sizeof(mbuf));
  if (n <= 0) {
    int k;

    path[0] = '/';
    for (k = 0; argv[0][k] && k < (int)sizeof(path) - 2; k++)
      path[k + 1] = argv[0][k];
    path[k + 1] = 0;
    n = read_file(path, mbuf, sizeof(mbuf));
  }
  if (n <= 0) {
    printf("ld.so: cannot read main executable %s\n", argv[0]);
    exit(1);
  }
  mlen = n;
  e = (struct ehdr *)mbuf;
  if (e->ident[0] != 0x7f || e->ident[1] != 'E') {
    printf("ld.so: bad main elf\n");
    exit(1);
  }

  // 主程序的动态段（vaddr 已是真实地址）
  char *needed[MAXLIBS];
  int nneeded = 0;
  uint64 jmprel = 0, pltrelsz = 0, symtab = 0, strtab = 0;

  for (i = 0; i < e->phnum; i++) {
    struct phdr *ph = (struct phdr *)(mbuf + e->phoff + i * e->phentsize);
    uint64 *d;

    if (ph->type != PT_DYNAMIC)
      continue;
    d = (uint64 *)ph->vaddr;
    for (; d[0] != DT_NULL; d += 2) {
      switch (d[0]) {
      case DT_NEEDED:
        if (nneeded < MAXLIBS)
          needed[nneeded++] = (char *)d[1]; // 先记偏移，strtab 解析后再取
        break;
      case DT_JMPREL:
        jmprel = d[1];
        break;
      case DT_PLTRELSZ:
        pltrelsz = d[1];
        break;
      case DT_SYMTAB:
        symtab = d[1];
        break;
      case DT_STRTAB:
        strtab = d[1];
        break;
      default:
        break;
      }
    }
  }

  for (i = 0; i < nneeded; i++) {
    char *name = (char *)(strtab + (uint64)needed[i]);
    uint64 base;

    if (lookup(name)) // 已加载
      continue;
    base = map_lib(name, nlibs);
    if (base == 0) {
      printf("ld.so: failed to load %s\n", name);
      exit(1);
    }
    printf("ld.so: loaded %s at 0x%x\n", name, (uint)base);
  }

  // 主程序的 JUMP_SLOT：GOT 里填上库里函数地址
  if (jmprel && pltrelsz) {
    struct rela *r = (struct rela *)jmprel;
    int cnt = pltrelsz / sizeof(struct rela);

    for (i = 0; i < cnt; i++) {
      uint32 type = (uint32)(r[i].info & 0xffffffff);
      uint32 sidx = (uint32)(r[i].info >> 32);
      struct sym *s = (struct sym *)(symtab + sidx * sizeof(struct sym));
      char *name = (char *)(strtab + s->name);
      uint64 addr = lookup(name);

      if (type != R_RISCV_JUMP_SLOT)
        continue;
      if (addr == 0) {
        printf("ld.so: unresolved symbol %s\n", name);
        exit(1);
      }
      *(uint64 *)r[i].offset = addr;
    }
  }

  int (*start)(int, char **) = (int (*)(int, char **))entry;
  exit(start(argc, argv));
}
