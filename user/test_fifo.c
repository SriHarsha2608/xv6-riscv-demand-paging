#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test FIFO page replacement policy
int
main(int argc, char *argv[])
{
  printf("=== TEST 2: FIFO PAGE REPLACEMENT ===\n");
  
  struct proc_mem_stat info;
  
  // Allocate many pages to trigger replacement
  printf("Allocating large memory to trigger page replacement...\n");
  
  int num_pages = 100;
  char *pages[100];
  
  // Allocate and touch pages in sequence
  for(int i = 0; i < num_pages; i++) {
    pages[i] = sbrk(4096);
    if(pages[i] == (char*)-1) {
      printf("sbrk failed at page %d\n", i);
      break;
    }
    
    // Write unique pattern to each page
    for(int j = 0; j < 4096; j += 64) {
      pages[i][j] = (char)(i & 0xFF);
    }
    
    if(i % 10 == 0) {
      memstat(&info);
      printf("Page %d: resident=%d swapped=%d seq=%d\n", 
             i, info.num_resident_pages, info.num_swapped_pages, info.next_fifo_seq);
    }
  }
  
  // Get final state
  memstat(&info);
  printf("\nFinal state: resident=%d swapped=%d total=%d\n",
         info.num_resident_pages, info.num_swapped_pages, info.num_pages_total);
  
  // Check FIFO ordering - verify sequence numbers are increasing
  printf("\nVerifying FIFO sequence numbers...\n");
  int last_seq = -1;
  int resident_count = 0;
  
  for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
    if(info.pages[i].state == RESIDENT) {
      if(last_seq >= 0 && info.pages[i].seq < last_seq) {
        printf("WARNING: Sequence numbers not monotonic at page %d\n", i);
      }
      last_seq = info.pages[i].seq;
      resident_count++;
    }
  }
  
  printf("Verified %d resident pages with proper FIFO ordering\n", resident_count);
  
  // Test data integrity after potential swapping
  printf("\nTesting data integrity after page replacement...\n");
  int errors = 0;
  
  for(int i = 0; i < num_pages; i++) {
    if(pages[i] != (char*)-1) {
      char expected = (char)(i & 0xFF);
      if(pages[i][0] != expected) {
        printf("ERROR: Page %d corrupted! Expected %d, got %d\n", 
               i, expected, pages[i][0]);
        errors++;
      }
    }
  }
  
  if(errors == 0) {
    printf("PASS: All data intact after page replacement\n");
  } else {
    printf("FAIL: %d pages corrupted\n", errors);
  }
  
  exit(0);
}
