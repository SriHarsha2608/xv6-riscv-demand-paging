#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  struct proc_mem_stat info;
  
  printf("Testing memstat syscall...\n");
  
  if(memstat(&info) < 0) {
    printf("memstat failed\n");
    exit(1);
  }
  
  printf("pid: %d\n", info.pid);
  printf("next_fifo_seq: %d\n", info.next_fifo_seq);
  printf("num_resident_pages: %d\n", info.num_resident_pages);
  printf("num_swapped_pages: %d\n", info.num_swapped_pages);
  printf("num_pages_total: %d\n", info.num_pages_total);
  
  printf("First few pages:\n");
  for(int i = 0; i < 10 && i < MAX_PAGES_INFO; i++) {
    const char* state_str;
    switch(info.pages[i].state) {
      case UNMAPPED: state_str = "UNMAPPED"; break;
      case RESIDENT: state_str = "RESIDENT"; break;
      case SWAPPED: state_str = "SWAPPED"; break;
      default: state_str = "UNKNOWN"; break;
    }
    
    printf("  va=0x%x state=%s seq=%d dirty=%d swap_slot=%d\n",
           info.pages[i].va, state_str, info.pages[i].seq,
           info.pages[i].is_dirty, info.pages[i].swap_slot);
  }
  
  // Allocate some memory to trigger more page faults
  printf("\nAllocating memory...\n");
  char *ptr = sbrk(4096);
  if(ptr == (char*)-1) {
    printf("sbrk failed\n");
    exit(1);
  }
  
  // Touch the memory to trigger page fault
  *ptr = 42;
  
  // Check memstat again
  if(memstat(&info) < 0) {
    printf("memstat failed\n");
    exit(1);
  }
  
  printf("\nAfter allocation:\n");
  printf("num_resident_pages: %d\n", info.num_resident_pages);
  printf("num_pages_total: %d\n", info.num_pages_total);
  
  exit(0);
}