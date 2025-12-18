#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "memstat.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;
  struct proc *p = myproc();

  argint(0, &n);
  argint(1, &t);
  addr = p->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
    // When shrinking, remove pages[] entries for freed region
    if(n < 0) {
      uint64 newsz = p->sz;
      for(int i = 0; i < p->npages; ) {
        if(p->pages[i].va >= newsz) {
          // This page is beyond new heap; remove it
          // Shift remaining entries down
          for(int j = i; j < p->npages - 1; j++) {
            p->pages[j] = p->pages[j + 1];
          }
          p->npages--;
        } else {
          i++;
        }
      }
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    p->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Get memory statistics for the calling process
uint64
sys_memstat(void)
{
  uint64 addr;
  struct proc *p = myproc();
  struct proc_mem_stat st;
  int i;
  uint64 page_va;

  argaddr(0, &addr);

  // Clear structure
  memset(&st, 0, sizeof(st));

  // Basic process info
  st.pid = p->pid;
  st.next_fifo_seq = p->next_seq;
  st.num_pages_total = PGROUNDUP(p->sz) / PGSIZE;

  // Fill page info array
  for(i = 0; i < p->npages && i < MAX_PAGES_INFO; i++) {
    // Set basic page info
    st.pages[i].va = p->pages[i].va;
    st.pages[i].is_dirty = p->pages[i].dirty;
    st.pages[i].seq = p->pages[i].seq;

    // Set page state and update counters
    if(p->pages[i].resident) {
      st.pages[i].state = RESIDENT;
      st.pages[i].swap_slot = -1;
      st.num_resident_pages++;
    }
    else if(p->pages[i].swapped) {
      st.pages[i].state = SWAPPED;
      st.pages[i].swap_slot = p->pages[i].swap_offset;
      st.num_swapped_pages++;
    }
    else {
      st.pages[i].state = UNMAPPED;
      st.pages[i].swap_slot = -1;
    }
  }

  // Fill remaining slots with unmapped pages if there's room
  page_va = 0;
  for(; i < MAX_PAGES_INFO && page_va < p->sz; page_va += PGSIZE) {
    if(find_page_info(p, page_va) == 0) {
      st.pages[i].va = page_va;
      st.pages[i].state = UNMAPPED;
      st.pages[i].is_dirty = 0;
      st.pages[i].seq = 0;
      st.pages[i].swap_slot = -1;
      i++;
    }
  }

  // Copy to user space
  if(copyout(p->pagetable, addr, (char*)&st, sizeof(st)) < 0)
    return -1;

  return 0;
}
