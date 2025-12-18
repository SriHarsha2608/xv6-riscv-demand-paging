#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "fcntl.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;   
    if((*pte & PTE_V) == 0)  // has physical page been allocated?
      continue;
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // Leaf PTE still present (demand paging may leave these)
      // Free the physical page and clear PTE
      uint64 pa = PTE2PA(pte);
      kfree((void*)pa);
      pagetable[i] = 0;
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;   // page table entry hasn't been allocated
    if((*pte & PTE_V) == 0)
      continue;   // physical page hasn't been allocated
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Helper: check if a user VA is potentially valid (segment/heap/stack/swap)
static int
is_valid_user_va(struct proc *p, uint64 va)
{
  va = PGROUNDDOWN(va);
  if(va >= MAXVA)
    return 0;

  // Swapped-out known page
  struct page_info *pi = find_page_info(p, va);
  if(pi && pi->swapped)
    return 1;

  // In a loaded segment
  for(int i = 0; i < p->nsegments; i++) {
    if(va >= p->segments[i].vaddr && va < p->segments[i].vaddr + p->segments[i].memsz)
      return 1;
  }

  // In heap
  if(va >= p->heap_start && va < PGROUNDUP(p->sz) && va < p->stack_bottom)
    return 1;

  // In (growable) stack: within one page below SP and above stack_bottom
  uint64 sp = p->trapframe->sp;
  if(va >= p->stack_bottom && va < PGROUNDUP(p->sz) &&
     (va >= PGROUNDDOWN(sp) - PGSIZE || sp >= p->sz))
    return 1;

  return 0;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
  
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      // Only try to fault-in if the VA looks valid for this process.
      struct proc *p = myproc();
      if(!is_valid_user_va(p, va0))
        return -1;
      if((pa0 = vmfault(pagetable, va0, 15)) == 0)
        return -1;
    }

    pte = walk(pagetable, va0, 0);
    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
      
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    if(va0 >= MAXVA)
      return -1;
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      // Only try to fault-in if the VA looks valid for this process.
      struct proc *p = myproc();
      if(!is_valid_user_va(p, va0))
        return -1;
      if((pa0 = vmfault(pagetable, va0, 13)) == 0)
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    if(va0 >= MAXVA)
      return -1;
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      // Only try to fault-in if the VA looks valid for this process.
      struct proc *p = myproc();
      if(!is_valid_user_va(p, va0))
        return -1;
      if((pa0 = vmfault(pagetable, va0, 13)) == 0)
        return -1;
    }
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

// Find page info for a virtual address
struct page_info*
find_page_info(struct proc *p, uint64 va)
{
  va = PGROUNDDOWN(va);
  for(int i = 0; i < p->npages; i++) {
    if(p->pages[i].va == va)
      return &p->pages[i];
  }
  return 0;
}

// Add a new page to tracking
struct page_info*
add_page_info(struct proc *p, uint64 va)
{
  if(p->npages >= MAX_SWAP_PAGES)
    return 0;
  
  va = PGROUNDDOWN(va);
  struct page_info *pi = &p->pages[p->npages];
  pi->va = va;
  pi->seq = p->next_seq++;
  pi->dirty = 0;
  pi->swapped = 0;
  pi->swap_offset = 0;
  pi->resident = 1;
  p->npages++;
  return pi;
}

// Allocate a swap slot
// Returns slot number on success, -1 on failure
int
alloc_swap_slot(struct proc *p)
{
  // Check if we've reached the maximum
  if(p->nswap_slots >= MAX_SWAP_PAGES) {
    return -1;
  }
  
  // Find the first free slot in the bitmap
  for(int i = 0; i < MAX_SWAP_PAGES; i++) {
    int word_idx = i / 32;
    int bit_idx = i % 32;
    
    if((p->swap_slots[word_idx] & (1 << bit_idx)) == 0) {
      // Found a free slot
      p->swap_slots[word_idx] |= (1 << bit_idx);
      p->nswap_slots++;
      return i;
    }
  }
  
  return -1;
}

// Free a swap slot
void
free_swap_slot(struct proc *p, int slot)
{
  if(slot < 0 || slot >= MAX_SWAP_PAGES)
    return;
    
  int word_idx = slot / 32;
  int bit_idx = slot % 32;
  
  // Clear the bit
  if(p->swap_slots[word_idx] & (1 << bit_idx)) {
    p->swap_slots[word_idx] &= ~(1 << bit_idx);
    p->nswap_slots--;
  }
}

// Create swap file for process
int
create_swapfile(struct proc *p)
{
  char path[32];
  // Format: /pgswpXXXXX where XXXXX is PID
  path[0] = '/';
  path[1] = 'p';
  path[2] = 'g';
  path[3] = 's';
  path[4] = 'w';
  path[5] = 'p';
  
  // Convert PID to string (5 digits)
  int pid = p->pid;
  for(int i = 10; i >= 6; i--) {
    path[i] = '0' + (pid % 10);
    pid /= 10;
  }
  path[11] = 0;
  
  begin_op();
  struct inode *ip = create(path, T_FILE, 0, 0);
  if(ip == 0) {
    end_op();
    return -1;
  }
  
  // Allocate a file structure
  struct file *f = filealloc();
  if(f == 0) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  
  f->type = FD_INODE;
  f->off = 0;
  f->ip = ip;
  f->readable = 1;
  f->writable = 1;
  iunlock(ip);
  end_op();
  
  p->swapfile = f;
  return 0;
}

// Delete swap file for process
void
delete_swapfile(struct proc *p)
{
  if(p->swapfile == 0)
    return;
  
  // Log swap cleanup with number of slots reclaimed
  int slots_reclaimed = p->nswap_slots;
  printf("[pid %d] SWAPCLEANUP freed_slots=%d\n", p->pid, slots_reclaimed);
  
  // Close the file
  fileclose(p->swapfile);
  p->swapfile = 0;
  
  // Clear swap slot bitmap
  for(int i = 0; i < MAX_SWAP_PAGES / 32; i++) {
    p->swap_slots[i] = 0;
  }
  p->nswap_slots = 0;
  
  // Note: We don't explicitly delete the swap file here to avoid
  // locking issues. The file will remain on disk but that's OK for now.
  // In a production system, we'd mark it for cleanup by a background task.
}

// Evict a page using FIFO policy
// Evicts ONLY from this process's own resident set (per-process replacement)
uint64
evict_page(struct proc *p)
{
  // Find the page with the lowest sequence number (oldest) from resident set
  // Note: Using uint64 for sequence numbers means wraparound occurs after 2^64
  // allocations, which is practically impossible in xv6's lifetime
  struct page_info *victim = 0;
  uint64 min_seq = ~0ULL;
  
  for(int i = 0; i < p->npages; i++) {
    if(p->pages[i].resident && p->pages[i].seq < min_seq) {
      min_seq = p->pages[i].seq;
      victim = &p->pages[i];
    }
  }
  
  if(victim == 0)
    return 0;
  
  uint64 va = victim->va;
  uint64 victim_seq = victim->seq;
  
  // Log the victim selection
  printf("[pid %d] VICTIM va=0x%lx seq=%d algo=FIFO\n", p->pid, va, (int)victim_seq);
  
  pte_t *pte = walk(p->pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0)
    return 0;
  
  uint64 pa = PTE2PA(*pte);
  
  // Check if page has a valid backing store (can be reloaded from executable)
  int has_backing_store = 0;
  for(int i = 0; i < p->nsegments; i++) {
    if(va >= p->segments[i].vaddr && va < p->segments[i].vaddr + p->segments[i].memsz) {
      // Page is in text/data segment and can be reloaded from executable
      uint64 offset_in_seg = va - p->segments[i].vaddr;
      if(offset_in_seg < p->segments[i].filesz) {
        has_backing_store = 1;
      }
      break;
    }
  }
  
  // Determine if we need to swap out or can discard
  int should_swap = 0;
  
  // If dirty OR no backing store, must write to swap
  if(victim->dirty || !has_backing_store) {
    should_swap = 1;
  }
  
  if(should_swap) {
    // Write page to swap file
    if(swapout_page(p, va) < 0) {
      // Swap is full - terminate process
      printf("[pid %d] SWAPFULL\n", p->pid);
      printf("[pid %d] KILL swap-exhausted\n", p->pid);
      p->killed = 1;
      return 0;
    }
    
    victim->swapped = 1;
    victim->resident = 0;
    printf("[pid %d] EVICT va=0x%lx state=%s\n", p->pid, va, victim->dirty ? "dirty" : "clean");
  } else {
    // Clean page with backing store - can be discarded
    printf("[pid %d] EVICT va=0x%lx state=clean\n", p->pid, va);
    printf("[pid %d] DISCARD va=0x%lx\n", p->pid, va);
    
    // Remove page_info entry since page can be reloaded from executable
    // Shift all subsequent entries down
    int victim_idx = victim - p->pages;
    for(int i = victim_idx; i < p->npages - 1; i++) {
      p->pages[i] = p->pages[i + 1];
    }
    p->npages--;
  }
  
  // Free the physical page
  kfree((void*)pa);
  
  // Invalidate PTE
  *pte = 0;
  
  return pa;
}

// Swap out a page to disk
int
swapout_page(struct proc *p, uint64 va)
{
  va = PGROUNDDOWN(va);
  
  struct page_info *pi = find_page_info(p, va);
  if(pi == 0)
    return -1;
  
  // Create swap file if it doesn't exist
  if(p->swapfile == 0) {
    if(create_swapfile(p) < 0) {
      return -1;
    }
  }
  
  // Allocate a swap slot if not already allocated
  int slot = pi->swap_offset;
  if(!pi->swapped) {
    slot = alloc_swap_slot(p);
    if(slot < 0) {
      // No free swap slots - swap full
      printf("[pid %d] SWAPFULL\n", p->pid);
      return -1;
    }
    pi->swap_offset = slot;
  }
  
  // Get the physical address
  pte_t *pte = walk(p->pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0)
    return -1;
  uint64 pa = PTE2PA(*pte);
  
  // Write to swap file
  begin_op();
  ilock(p->swapfile->ip);
  
  // Write page to swap file at offset (slot * PGSIZE)
  int n = writei(p->swapfile->ip, 0, pa, slot * PGSIZE, PGSIZE);
  
  iunlock(p->swapfile->ip);
  end_op();
  
  if(n != PGSIZE) {
    return -1;
  }
  
  printf("[pid %d] SWAPOUT va=0x%lx slot=%d\n", p->pid, va, slot);
  return 0;
}

// Swap in a page from disk
int
swapin_page(struct proc *p, uint64 va)
{
  va = PGROUNDDOWN(va);
  
  struct page_info *pi = find_page_info(p, va);
  if(pi == 0 || !pi->swapped)
    return -1;
  
  // Allocate physical page
  uint64 mem = (uint64)kalloc();
  if(mem == 0) {
    // Out of memory during swap-in
    printf("[pid %d] MEMFULL\n", p->pid);
    
    // Try to evict a page
    if(evict_page(p) == 0)
      return -1;
    
    mem = (uint64)kalloc();
    if(mem == 0)
      return -1;
  }
  
  // Read from swap file
  if(p->swapfile == 0) {
    kfree((void*)mem);
    return -1;
  }
  
  int slot = pi->swap_offset;
  
  begin_op();
  ilock(p->swapfile->ip);
  
  // Read page from swap file at offset (slot * PGSIZE)
  int n = readi(p->swapfile->ip, 0, mem, slot * PGSIZE, PGSIZE);
  
  iunlock(p->swapfile->ip);
  end_op();
  
  if(n != PGSIZE) {
    kfree((void*)mem);
    return -1;
  }
  
  printf("[pid %d] SWAPIN va=0x%lx slot=%d\n", p->pid, va, slot);
  
  // Get permissions from segment info or default to RW
  int perm = PTE_U | PTE_R | PTE_W;
  for(int i = 0; i < p->nsegments; i++) {
    if(va >= p->segments[i].vaddr && va < p->segments[i].vaddr + p->segments[i].memsz) {
      perm = p->segments[i].perm | PTE_U | PTE_R;
      break;
    }
  }
  
  // Map the page
  if(mappages(p->pagetable, va, PGSIZE, mem, perm) != 0) {
    kfree((void*)mem);
    return -1;
  }
  
  // Free the swap slot since page is now in memory
  free_swap_slot(p, slot);
  
  pi->resident = 1;
  pi->seq = p->next_seq++;
  pi->dirty = 0;
  pi->swapped = 0;  // No longer swapped
  
  printf("[pid %d] RESIDENT va=0x%lx seq=%d\n", p->pid, va, (int)pi->seq);
  
  return 0;
}

// Mark page as dirty
void
mark_page_dirty(struct proc *p, uint64 va)
{
  struct page_info *pi = find_page_info(p, va);
  if(pi)
    pi->dirty = 1;
}

// Enhanced page fault handler for demand paging
// scause: 12=exec, 13=read, 15=write
uint64
vmfault(pagetable_t pagetable, uint64 va, uint64 scause)
{
  struct proc *p = myproc();
  uint64 mem;
  
  va = PGROUNDDOWN(va);
  
  // Determine access type from scause
  const char *access_type;
  int is_write = 0;
  if(scause == 12) {
    access_type = "exec";
  } else if(scause == 13) {
    access_type = "read";
  } else { // scause == 15
    access_type = "write";
    is_write = 1;
  }
  
  // Check for invalid addresses (>= MAXVA, e.g., kernel addresses)
  if(va >= MAXVA) {
    printf("[pid %d] KILL invalid-access va=0x%lx access=%s\n", p->pid, va, access_type);
    p->killed = 1;
    return 0;
  }

  // Check if already mapped
  if(ismapped(pagetable, va)) {
    // Validate permissions for the faulting access type.
    pte_t *pte = walk(pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
      // Treat as invalid mapping
      printf("[pid %d] KILL invalid-access va=0x%lx access=%s\n", p->pid, va, access_type);
      p->killed = 1;
      return 0;
    }

    // scause 12: exec, require X; 13: read, require R; 15: write, require W
    int ok = 1;
    if(scause == 12) {
      ok = ((*pte & PTE_X) != 0);
    } else if(scause == 13) {
      ok = ((*pte & PTE_R) != 0);
    } else { // write
      ok = ((*pte & PTE_W) != 0);
    }

    if(!ok) {
      // Protection fault: mapped but insufficient permissions.
      printf("[pid %d] KILL invalid-access va=0x%lx access=%s\n", p->pid, va, access_type);
      p->killed = 1;
      return 0;
    }

    // If write to a page, mark it dirty
    if(is_write) {
      mark_page_dirty(p, va);
    }
    return walkaddr(pagetable, va);
  }
  
  // Check if page was swapped out
  struct page_info *pi = find_page_info(p, va);
  if(pi && pi->swapped) {
    printf("[pid %d] PAGEFAULT va=0x%lx access=%s cause=swap\n", 
           p->pid, va, access_type);
    if(swapin_page(p, va) < 0) {
      return 0;
    }
    if(is_write)
      mark_page_dirty(p, va);
    return walkaddr(pagetable, va);
  }
  
  // Check if address is valid
  
  // 1. Check if in text/data segments
  int in_segment = 0;
  int seg_index = -1;
  for(int i = 0; i < p->nsegments; i++) {
    if(va >= p->segments[i].vaddr && va < p->segments[i].vaddr + p->segments[i].memsz) {
      in_segment = 1;
      seg_index = i;
      break;
    }
  }
  
  // 2. Check if in heap
  // Heap consists of two regions:
  //  - Original heap: [heap_start, stack_bottom)
  //  - Lazy-allocated heap: [stack_top, sz) if stack_top is set
  int in_heap = (va >= p->heap_start && va < p->stack_bottom);
  if(p->stack_top > 0) {
    in_heap = in_heap || (va >= p->stack_top && va < PGROUNDUP(p->sz));
  }
  
  // 3. Check if in stack (only within original stack region)
  // Stack is constrained to [stack_bottom, stack_top) if stack_top is set
  // Otherwise use old logic as fallback
  uint64 sp = p->trapframe->sp;
  int in_stack;
  if(p->stack_top > 0) {
    in_stack = (va >= p->stack_bottom && va < p->stack_top && 
                (va >= PGROUNDDOWN(sp) - PGSIZE || sp >= p->stack_top));
  } else {
    // Fallback for processes where stack_top not yet set
    in_stack = (va >= p->stack_bottom && va < PGROUNDUP(p->sz) && 
                (va >= PGROUNDDOWN(sp) - PGSIZE || sp >= p->sz));
  }
  
  if(!in_segment && !in_heap && !in_stack) {
    printf("[pid %d] KILL invalid-access va=0x%lx access=%s\n", p->pid, va, access_type);
    p->killed = 1;
    return 0;
  }
  
  // Allocate physical memory
  mem = (uint64)kalloc();
  if(mem == 0) {
    // Out of memory - trigger page replacement
    printf("[pid %d] MEMFULL\n", p->pid);
    
    // Try to evict a page from this process's resident set
    if(evict_page(p) == 0) {
      return 0;
    }
    
    mem = (uint64)kalloc();
    if(mem == 0) {
      return 0;
    }
  }
  
  // Handle different page types
  if(in_segment) {
    // Load from executable
    printf("[pid %d] PAGEFAULT va=0x%lx access=%s cause=exec\n", 
           p->pid, va, access_type);
    
    struct prog_segment *seg = &p->segments[seg_index];
    uint64 offset_in_seg = va - seg->vaddr;
    uint64 file_offset = seg->off + offset_in_seg;
    
    memset((void*)mem, 0, PGSIZE);
    
    // Load from file if within filesz
    if(offset_in_seg < seg->filesz) {
      uint64 to_read = PGSIZE;
      if(offset_in_seg + PGSIZE > seg->filesz)
        to_read = seg->filesz - offset_in_seg;
      
      ilock(p->exec_inode);
      if(readi(p->exec_inode, 0, mem, file_offset, to_read) != to_read) {
        iunlock(p->exec_inode);
        kfree((void*)mem);
        return 0;
      }
      iunlock(p->exec_inode);
    }
    
    // Map with appropriate permissions
    int perm = seg->perm | PTE_U | PTE_R;
    if(mappages(pagetable, va, PGSIZE, mem, perm) != 0) {
      kfree((void*)mem);
      return 0;
    }
    
    printf("[pid %d] LOADEXEC va=0x%lx\n", p->pid, va);
    
  } else if(in_heap) {
    // Zero-filled heap page
    printf("[pid %d] PAGEFAULT va=0x%lx access=%s cause=heap\n", 
           p->pid, va, access_type);
    
    memset((void*)mem, 0, PGSIZE);
    
    if(mappages(pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
      kfree((void*)mem);
      return 0;
    }
    
    printf("[pid %d] ALLOC va=0x%lx\n", p->pid, va);
    
  } else if(in_stack) {
    // Zero-filled stack page
    printf("[pid %d] PAGEFAULT va=0x%lx access=%s cause=stack\n", 
           p->pid, va, access_type);
    
    memset((void*)mem, 0, PGSIZE);
    
    if(mappages(pagetable, va, PGSIZE, mem, PTE_W | PTE_U | PTE_R) != 0) {
      kfree((void*)mem);
      return 0;
    }
    
    printf("[pid %d] ALLOC va=0x%lx\n", p->pid, va);
  }
  
  // Add to page tracking
  pi = add_page_info(p, va);
  if(pi == 0) {
    // Too many pages - this shouldn't happen
    uvmunmap(pagetable, va, 1, 1);
    return 0;
  }
  
  if(is_write)
    pi->dirty = 1;
  
  printf("[pid %d] RESIDENT va=0x%lx seq=%d\n", p->pid, va, (int)pi->seq);
  
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}
