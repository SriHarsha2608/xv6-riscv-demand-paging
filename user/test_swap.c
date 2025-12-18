#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test swap file operations and dirty page tracking
int
main(int argc, char *argv[])
{
  printf("=== TEST 3: SWAP FILE OPERATIONS ===\n");
  
  struct proc_mem_stat info;
  
  printf("PID: %d\n", getpid());
  printf("Swap file should be created at /pgswp%05d\n", getpid());
  
  // Allocate many pages to force swapping
  printf("\nAllocating 200 pages to trigger swapping...\n");
  
  char *pages[200];
  int allocated = 0;
  
  for(int i = 0; i < 200; i++) {
    pages[i] = sbrk(4096);
    if(pages[i] == (char*)-1) {
      printf("sbrk failed at page %d\n", i);
      break;
    }
    allocated++;
    
    // Write unique pattern - marks page as dirty
    for(int j = 0; j < 256; j++) {
      pages[i][j * 16] = (char)(i + j);
    }
    
    if(i % 20 == 0) {
      memstat(&info);
      printf("Allocated %d: resident=%d swapped=%d\n", 
             i, info.num_resident_pages, info.num_swapped_pages);
    }
  }
  
  printf("\nAllocated %d pages total\n", allocated);
  
  // Check swap state
  memstat(&info);
  printf("\nCurrent state:\n");
  printf("  Resident pages: %d\n", info.num_resident_pages);
  printf("  Swapped pages: %d\n", info.num_swapped_pages);
  printf("  Total pages: %d\n", info.num_pages_total);
  printf("  Next FIFO seq: %d\n", info.next_fifo_seq);
  
  if(info.num_swapped_pages > 0) {
    printf("\n✓ Swapping occurred! Testing swap-in...\n");
    
    // Find a swapped page
    int swapped_idx = -1;
    for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
      if(info.pages[i].state == SWAPPED) {
        swapped_idx = i;
        printf("Found swapped page at index %d, slot=%d\n", 
               i, info.pages[i].swap_slot);
        break;
      }
    }
    
    if(swapped_idx >= 0) {
      // Access the swapped page to trigger swap-in
      uint va = info.pages[swapped_idx].va;
      printf("Accessing swapped page at va=0x%x...\n", va);
      
      // Calculate which page array index corresponds to this VA
      char *ptr = (char*)(unsigned long)va;
      char data = *ptr;  // Trigger swap-in
      
      printf("Successfully read data: %d\n", data);
      
      // Verify page is now resident
      memstat(&info);
      int found_resident = 0;
      for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
        if(info.pages[i].va == va && info.pages[i].state == RESIDENT) {
          found_resident = 1;
          printf("✓ Page now resident at seq=%d\n", info.pages[i].seq);
          break;
        }
      }
      
      if(found_resident) {
        printf("PASS: Swap-in successful\n");
      } else {
        printf("FAIL: Page not resident after access\n");
      }
    }
  } else {
    printf("\nNo swapping occurred (sufficient memory)\n");
  }
  
  // Test data integrity across all pages
  printf("\nVerifying data integrity across all pages...\n");
  int errors = 0;
  int checked = 0;
  
  for(int i = 0; i < allocated; i++) {
    if(pages[i] != (char*)-1) {
      char expected = (char)(i + 0);  // First byte pattern
      if(pages[i][0] != expected) {
        errors++;
        if(errors < 5) {  // Only print first few errors
          printf("ERROR: Page %d corrupted! Expected %d, got %d\n", 
                 i, expected, pages[i][0]);
        }
      }
      checked++;
    }
  }
  
  printf("Checked %d pages, found %d errors\n", checked, errors);
  
  if(errors == 0) {
    printf("PASS: All data intact after swapping\n");
  } else {
    printf("FAIL: Data corruption detected\n");
  }
  
  printf("\nNote: Swap file /pgswp%05d will be cleaned up on exit\n", getpid());
  
  exit(0);
}
