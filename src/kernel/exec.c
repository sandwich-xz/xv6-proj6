#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// map ELF permissions to PTE permission bits.
int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x1)
      perm = PTE_X;
    if(flags & 0x2)
      perm |= PTE_W;
    return perm;
}

//
// the implementation of the exec() system call
//
int
kexec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();
  char interp[MAXPATH];
  int hasinterp = 0;
  uint64 interp_entry = 0, interp_base = 0, interp_size = 0;

  begin_op();

  // Open the executable file.
  ip = namei(path);
  if(ip == 0 && path[0] != '/'){
    // Bare command names (no '/') fall back to the root directory,
    // so programs installed in "/" work from any working directory.
    int i, slash;
    char rootpath[MAXPATH];

    slash = 0;
    for(i = 0; path[i]; i++)
      if(path[i] == '/')
        slash = 1;
    if(!slash && strlen(path) + 2 <= sizeof(rootpath)){
      rootpath[0] = '/';
      safestrcpy(rootpath + 1, path, sizeof(rootpath) - 1);
      ip = namei(rootpath);
    }
  }
  if(ip == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Read the ELF header.
  if(vfs_readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // Is this really an ELF file?
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(vfs_readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type == ELF_PROG_INTERP){
      if(ph.filesz >= sizeof(interp))
        goto bad;
      if(vfs_readi(ip, 0, (uint64)interp, ph.off, (uint)ph.filesz) != ph.filesz)
        goto bad;
      interp[ph.filesz] = 0;
      hasinterp = 1;
      continue;
    }
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // 如果可执行文件带 PT_INTERP，把解释器（ld.so）映射到它自己的固定地址。
  // 解释器单独占一段地址，不并入 sz，否则会把 0x30000000 以下的空洞全部
  // 按页映射出来。
  if(hasinterp){
    struct elfhdr ielf;
    struct proghdr iph;
    struct inode *iip;
    uint64 imin = ~0ULL, imax = 0;
    int k;

    iip = namei(interp);
    if(iip == 0)
      goto bad;
    ilock(iip);
    if(vfs_readi(iip, 0, (uint64)&ielf, 0, sizeof(ielf)) != sizeof(ielf) ||
       ielf.magic != ELF_MAGIC){
      iunlockput(iip);
      goto bad;
    }
    for(k = 0; k < ielf.phnum; k++){
      if(vfs_readi(iip, 0, (uint64)&iph, ielf.phoff + k*sizeof(iph), sizeof(iph)) != sizeof(iph)){
        iunlockput(iip);
        goto bad;
      }
      if(iph.type != ELF_PROG_LOAD)
        continue;
      if(iph.vaddr < imin)
        imin = iph.vaddr;
      if(iph.vaddr + iph.memsz > imax)
        imax = iph.vaddr + iph.memsz;
    }
    if(imax == 0){
      iunlockput(iip);
      goto bad;
    }
    interp_base = imin & ~(PGSIZE - 1);
    interp_size = PGROUNDUP(imax) - interp_base;
    if(uvmalloc(pagetable, interp_base, interp_base + interp_size, PTE_W | PTE_X) == 0){
      iunlockput(iip);
      goto bad;
    }
    for(k = 0; k < ielf.phnum; k++){
      if(vfs_readi(iip, 0, (uint64)&iph, ielf.phoff + k*sizeof(iph), sizeof(iph)) != sizeof(iph)){
        iunlockput(iip);
        goto bad;
      }
      if(iph.type != ELF_PROG_LOAD)
        continue;
      if(loadseg(pagetable, iph.vaddr, iip, iph.off, iph.filesz) < 0){
        iunlockput(iip);
        goto bad;
      }
    }
    interp_entry = ielf.entry;
    iunlockput(iip);
  }

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK*PGSIZE;

  // Copy argument strings into new stack, remember their
  // addresses in ustack[].
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // 有解释器时，auxv 要放在 argv 之上（高地址），这样 ld.so 从
  // argv[argc]==NULL 之后就能顺序读到 AT_ENTRY 等。
  if(hasinterp){
    uint64 auxv[8];

    auxv[0] = 9;            // AT_ENTRY
    auxv[1] = elf.entry;
    auxv[2] = 7;            // AT_BASE
    auxv[3] = interp_base;
    auxv[4] = 6;            // AT_PAGESZ
    auxv[5] = PGSIZE;
    auxv[6] = 0;            // AT_NULL
    auxv[7] = 0;

    sp -= sizeof(auxv);
    sp -= sp % 16;
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, (char *)auxv, sizeof(auxv)) < 0)
      goto bad;
  }

  // push a copy of ustack[], the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // a0 and a1 contain arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  uint64 old_interp_base = p->interp_base, old_interp_size = p->interp_size;
  p->pagetable = pagetable;
  p->sz = sz;
  p->interp_base = interp_base;
  p->interp_size = interp_size;
  uint64 entry = hasinterp ? interp_entry : elf.entry;
#ifdef ARCH_LOONGARCH
  p->trapframe->era = entry;  // initial program counter
#else
  p->trapframe->epc = entry;  // initial program counter
#endif
  p->trapframe->sp = sp; // initial stack pointer
  if(old_interp_size)
    uvmunmap(oldpagetable, old_interp_base, old_interp_size/PGSIZE, 1);
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable){
    if(interp_size)
      uvmunmap(pagetable, interp_base, interp_size/PGSIZE, 1);
    proc_freepagetable(pagetable, sz);
  }
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
#ifdef ARCH_LOONGARCH
    pa |= DMWIN_MASK;
#endif
    if(vfs_readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
