// Test program for memstat system call
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

void
print_state(int state)
{
  if(state == RESIDENT)
    printf("RESIDENT");
  else if(state == SWAPPED)
    printf("SWAPPED ");
  else
    printf("UNMAPPED");
}

int
main(int argc, char *argv[])
{
  struct proc_mem_stat info;
  
  printf("=== memstat System Call Test ===\n\n");
  
  // Get initial stats
  if(memstat(&info) < 0) {
    printf("ERROR: memstat failed\n");
    exit(1);
  }
  
  printf("Initial State:\n");
  printf("  PID: %d\n", info.pid);
  printf("  Total pages: %d\n", info.num_pages_total);
  printf("  Resident pages: %d\n", info.num_resident_pages);
  printf("  Swapped pages: %d\n", info.num_swapped_pages);
  printf("  Next FIFO seq: %d\n", info.next_fifo_seq);
  
  // Allocate some memory (2 pages = 8192 bytes)
  printf("\nAllocating 8192 bytes (2 pages)...\n");
  char *p = sbrk(8192);
  if(p == (char*)-1) {
    printf("ERROR: sbrk failed\n");
    exit(1);
  }
  
  // Touch first page
  printf("Writing to first page...\n");
  p[0] = 'A';
  p[100] = 'B';
  
  // Touch second page
  printf("Writing to second page...\n");
  p[4096] = 'C';
  p[5000] = 'D';
  
  // Get stats again
  if(memstat(&info) < 0) {
    printf("ERROR: memstat failed\n");
    exit(1);
  }
  
  printf("\nAfter Allocation:\n");
  printf("  PID: %d\n", info.pid);
  printf("  Total pages: %d\n", info.num_pages_total);
  printf("  Resident pages: %d\n", info.num_resident_pages);
  printf("  Swapped pages: %d\n", info.num_swapped_pages);
  printf("  Next FIFO seq: %d\n", info.next_fifo_seq);
  
  // Show page details (limit to 20 for readability)
  printf("\nPage Details (first 20 pages):\n");
  printf("  VA       State     Dirty Seq  Slot\n");
  printf("  -------- --------- ----- ---- ----\n");
  
  int count = info.num_pages_total < 20 ? info.num_pages_total : 20;
  for(int i = 0; i < count; i++) {
    printf("  0x%x ", info.pages[i].va);
    print_state(info.pages[i].state);
    printf("  %d     %d", info.pages[i].is_dirty, info.pages[i].seq);
    if(info.pages[i].state == SWAPPED)
      printf(" %d", info.pages[i].swap_slot);
    else
      printf(" -");
    printf("\n");
  }
  
  // Test with more memory to potentially trigger swapping
  printf("\nAllocating 40960 bytes (10 more pages)...\n");
  char *p2 = sbrk(40960);
  if(p2 != (char*)-1) {
    // Touch every page
    for(int i = 0; i < 10; i++) {
      p2[i * 4096] = 'X';
    }
    
    // Get final stats
    if(memstat(&info) < 0) {
      printf("ERROR: memstat failed\n");
      exit(1);
    }
    
    printf("\nFinal State:\n");
    printf("  PID: %d\n", info.pid);
    printf("  Total pages: %d\n", info.num_pages_total);
    printf("  Resident pages: %d\n", info.num_resident_pages);
    printf("  Swapped pages: %d\n", info.num_swapped_pages);
    printf("  Next FIFO seq: %d\n", info.next_fifo_seq);
  }
  
  printf("\n=== Test Complete ===\n");
  exit(0);
}

