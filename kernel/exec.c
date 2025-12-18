#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

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
  struct inode *old_exec_inode;

  begin_op();

  // Open the executable file.
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Read the ELF header.
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // Is this really an ELF file?
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Initialize demand paging metadata
  p->nsegments = 0;
  p->npages = 0;
  p->next_seq = 0;
  
  // Save program segments for demand loading (DO NOT load them now)
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    
    // Store segment info for demand loading
    if(p->nsegments >= 8)
      goto bad;
    p->segments[p->nsegments].vaddr = ph.vaddr;
    p->segments[p->nsegments].filesz = ph.filesz;
    p->segments[p->nsegments].memsz = ph.memsz;
    p->segments[p->nsegments].off = ph.off;
    p->segments[p->nsegments].perm = flags2perm(ph.flags);
    p->nsegments++;
    
    // Update sz to track the memory size (but don't allocate)
    if(ph.vaddr + ph.memsz > sz)
      sz = ph.vaddr + ph.memsz;
  }
  
  // Log initial memory map - need to identify text and data segments
  uint64 text_start = 0, text_end = 0, data_start = 0, data_end = 0;
  
  // Find text segment (executable, not writable)
  for(int i = 0; i < p->nsegments; i++) {
    if(p->segments[i].perm & PTE_X) {
      text_start = p->segments[i].vaddr;
      text_end = p->segments[i].vaddr + p->segments[i].memsz;
      break;
    }
  }
  
  // Find data segment (writable, not executable)
  for(int i = 0; i < p->nsegments; i++) {
    if((p->segments[i].perm & PTE_W) && !(p->segments[i].perm & PTE_X)) {
      data_start = p->segments[i].vaddr;
      data_end = p->segments[i].vaddr + p->segments[i].memsz;
      break;
    }
  }
  
  // Calculate stack_top (before allocating guard or stack pages)
  uint64 heap_start_addr = sz;
  uint64 stack_top = sz + USERSTACK*PGSIZE + PGSIZE; // +PGSIZE for guard page
  
  printf("[pid %d] INIT-LAZYMAP text=[0x%lx,0x%lx) data=[0x%lx,0x%lx) heap_start=0x%lx stack_top=0x%lx\n",
         p->pid,
         text_start, text_end,
         data_start, data_end,
         heap_start_addr,
         stack_top);

  // Keep the executable inode open for demand loading  
  old_exec_inode = p->exec_inode;
  p->exec_inode = idup(ip);
  
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Setup stack region
  sz = PGROUNDUP(sz);
  p->heap_start = sz;  // Heap starts after text/data
  
  // Reserve space for stack
  // Make the first inaccessible as a stack guard.
  uint64 stack_start = sz;
  sz = sz + (USERSTACK+1)*PGSIZE;
  
  // Create page table entry for stack guard (invalid)
  // DO NOT allocate physical page - just mark as invalid
  pte_t *pte = walk(pagetable, stack_start, 1);
  if(pte == 0)
    goto bad;
  *pte = 0;  // Invalid guard page
  
  sp = sz;
  stackbase = sp - USERSTACK*PGSIZE;
  p->stack_bottom = stackbase;
  p->stack_top = sz;  // Save original stack top for lazy allocation boundary
  
  // Allocate the initial stack page for arguments
  // This is needed because copyout happens before trapframe->sp is set
  char *stack_mem = kalloc();
  if(stack_mem == 0)
    goto bad;
  memset(stack_mem, 0, PGSIZE);
  if(mappages(pagetable, PGROUNDDOWN(stackbase), PGSIZE, (uint64)stack_mem, PTE_W | PTE_R | PTE_U) != 0) {
    kfree(stack_mem);
    goto bad;
  }
  
  // Add initial stack page to tracking
  if(p->npages < MAX_SWAP_PAGES) {
    struct page_info *pi = &p->pages[p->npages];
    pi->va = PGROUNDDOWN(stackbase);
    pi->seq = p->next_seq++;
    pi->dirty = 0;
    pi->swapped = 0;
    pi->swap_offset = 0;
    pi->resident = 1;
    p->npages++;
  }

  // Copy argument strings into new stack
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
  
  // Create swap file for this process
  // Note: We create it here during exec as per spec, though it may not be used
  if(create_swapfile(p) < 0) {
    // Failed to create swap file - not fatal, will be created on first use
    p->swapfile = 0;
  }
    
  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = ulib.c:start()
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
  
  // Clean up old exec inode if any
  if(old_exec_inode) {
    begin_op();
    iput(old_exec_inode);
    end_op();
  }

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
