// SPDX-License-Identifier: BSD-2-Clause
//
// 自己动手加载 .so：读 ELF、按 PT_LOAD 拷到指定基址、应用 R_RISCV_RELATIVE
// 重定位、按符号名查地址。这就是动态链接器的核心那几步，先不做 PT_INTERP。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define SHT_DYNSYM 11
#define DT_RELA 7
#define DT_RELASZ 8
#define R_RISCV_RELATIVE 3

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

static uchar filebuf[65536];
static int filelen;

static int
load_file(char *path)
{
  int fd, n, tot = 0;

  fd = open(path, 0);
  if (fd < 0)
    return -1;
  while ((n = read(fd, filebuf + tot, sizeof(filebuf) - tot)) > 0) {
    tot += n;
    if (tot == sizeof(filebuf))
      break;
  }
  close(fd);
  filelen = tot;
  return 0;
}

// 把 .so 映射到进程里，返回基址；0 表示失败。
static uint64
map_so(void)
{
  struct ehdr *e = (struct ehdr *)filebuf;
  struct phdr *ph;
  uint64 end = 0;
  uint64 base;
  int i;

  if (filelen < sizeof(*e) || e->ident[0] != 0x7f || e->ident[1] != 'E' ||
      e->ident[2] != 'L' || e->ident[3] != 'F')
    return 0;
  if (e->type != 3) // ET_DYN
    return 0;

  for (i = 0; i < e->phnum; i++) {
    ph = (struct phdr *)(filebuf + e->phoff + i * e->phentsize);
    if (ph->type == PT_LOAD && ph->vaddr + ph->memsz > end)
      end = ph->vaddr + ph->memsz;
  }
  if (end == 0)
    return 0;

  base = mmap(0, (int)end);
  if (base == (uint64)-1)
    return 0;

  for (i = 0; i < e->phnum; i++) {
    ph = (struct phdr *)(filebuf + e->phoff + i * e->phentsize);
    if (ph->type != PT_LOAD)
      continue;
    memmove((void *)(base + ph->vaddr), filebuf + ph->offset, ph->filesz);
    if (ph->memsz > ph->filesz)
      memset((void *)(base + ph->vaddr + ph->filesz), 0,
             ph->memsz - ph->filesz);
  }

  // 重定位：只处理 RELATIVE，够这个库用
  for (i = 0; i < e->phnum; i++) {
    struct rela *r;
    uint64 n;

    ph = (struct phdr *)(filebuf + e->phoff + i * e->phentsize);
    if (ph->type != PT_DYNAMIC)
      continue;
    {
      uint64 *d = (uint64 *)(base + ph->vaddr);
      uint64 rela = 0, relasz = 0;

      for (; d[0] != 0; d += 2) {
        if (d[0] == DT_RELA)
          rela = d[1];
        else if (d[0] == DT_RELASZ)
          relasz = d[1];
      }
      r = (struct rela *)(base + rela);
      n = relasz / sizeof(struct rela);
      for (uint64 k = 0; k < n; k++) {
        uint32 type = (uint32)(r[k].info & 0xffffffff);
        if (type == R_RISCV_RELATIVE) {
          *(uint64 *)(base + r[k].offset) = base + r[k].addend;
        }
      }
    }
  }
  return base;
}

static uint64
find_symbol(uint64 base, char *name)
{
  struct ehdr *e = (struct ehdr *)filebuf;
  struct shdr *sh;
  int i;

  for (i = 0; i < e->shnum; i++) {
    struct sym *s;
    uint64 cnt;
    char *str;

    sh = (struct shdr *)(filebuf + e->shoff + i * e->shentsize);
    if (sh->type != SHT_DYNSYM)
      continue;
    s = (struct sym *)(base + sh->addr);
    cnt = sh->size / sizeof(struct sym);
    str = (char *)(base + ((struct shdr *)(filebuf + e->shoff +
                                          sh->link * e->shentsize))->addr);
    for (uint64 k = 0; k < cnt; k++) {
      if (s[k].name && strcmp(str + s[k].name, name) == 0)
        return base + s[k].value;
    }
  }
  return 0;
}

int
main(void)
{
  uint64 base;
  int (*add)(int, int);
  int (*mul)(int, int);
  int (*fib)(int);
  uint64 addr;

  if (load_file("libmath.so") < 0) {
    printf("dltest: cannot read libmath.so\n");
    exit(1);
  }
  base = map_so();
  if (base == 0) {
    printf("dltest: map failed\n");
    exit(1);
  }

  addr = find_symbol(base, "add");
  if (addr == 0) {
    printf("dltest: symbol add not found\n");
    exit(1);
  }
  add = (int (*)(int, int))addr;

  addr = find_symbol(base, "mul");
  mul = (int (*)(int, int))addr;

  addr = find_symbol(base, "fib");
  fib = (int (*)(int))addr;

  printf("dltest: add(2,3)=%d mul(4,5)=%d fib(10)=%d\n", add(2, 3), mul(4, 5),
         fib(10));
  exit(0);
}
