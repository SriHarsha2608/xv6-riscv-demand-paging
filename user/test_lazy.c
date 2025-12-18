#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test basic lazy allocation and page fault handling
int
main(int argc, char *argv[])
{
  printf("=== TEST 1: LAZY ALLOCATION ===\n");
  
  struct proc_mem_stat info;
  
  // Get initial state
  if(memstat(&info) < 0) {
    printf("FAIL: memstat failed\n");
    exit(1);
  }
  
  int initial_resident = info.num_resident_pages;
  printf("Initial resident pages: %d\n", initial_resident);
  
  // Allocate 10 pages but don't touch them
  printf("\nAllocating 10 pages (40960 bytes) without touching...\n");
  char *base = sbrk(40960);
  if(base == (char*)-1) {
    printf("FAIL: sbrk failed\n");
    exit(1);
  }
  
  // Check - should NOT have allocated physical pages yet
  memstat(&info);
  printf("After sbrk: resident=%d (should be same as initial)\n", info.num_resident_pages);
  
  if(info.num_resident_pages > initial_resident + 2) {
    printf("FAIL: Pages allocated eagerly! Expected lazy allocation.\n");
    exit(1);
  }
  
  // Now touch first page - should trigger page fault
  printf("\nTouching first page...\n");
  base[0] = 'A';
  
  memstat(&info);
  printf("After touching page 0: resident=%d\n", info.num_resident_pages);
  
  // Touch middle page
  printf("Touching page 5...\n");
  base[5 * 4096] = 'B';
  
  memstat(&info);
  printf("After touching page 5: resident=%d\n", info.num_resident_pages);
  
  // Touch last page
  printf("Touching page 9...\n");
  base[9 * 4096] = 'C';
  
  memstat(&info);
  printf("After touching page 9: resident=%d\n", info.num_resident_pages);
  
  // Verify data integrity
  printf("\nVerifying data integrity...\n");
  if(base[0] != 'A' || base[5*4096] != 'B' || base[9*4096] != 'C') {
    printf("FAIL: Data corrupted!\n");
    exit(1);
  }
  
  printf("PASS: Lazy allocation working correctly\n");
  exit(0);
}
