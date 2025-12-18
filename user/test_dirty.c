#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test dirty page tracking and clean page eviction
int
main(int argc, char *argv[])
{
  printf("=== TEST 5: DIRTY PAGE TRACKING ===\n");
  
  struct proc_mem_stat info;
  
  // Test 1: Pages start clean after allocation
  printf("\n--- Test 5a: Initial clean state ---\n");
  char *p1 = sbrk(4096);
  if(p1 == (char*)-1) {
    printf("FAIL: sbrk failed\n");
    exit(1);
  }
  
  // Touch the page to allocate it (read-only initially)
  char dummy = *p1;
  (void)dummy;
  
  memstat(&info);
  printf("After read: allocated one page\n");
  
  // Find our page and check if it's clean
  int found = 0;
  for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
    if(info.pages[i].state == RESIDENT && info.pages[i].va == (uint)(unsigned long)p1) {
      printf("Page va=0x%x is_dirty=%d (should be 0 after just reading)\n", 
             info.pages[i].va, info.pages[i].is_dirty);
      found = 1;
      break;
    }
  }
  
  if(!found) {
    printf("WARNING: Could not find page in resident set\n");
  }
  
  // Test 2: Write makes page dirty
  printf("\n--- Test 5b: Write makes page dirty ---\n");
  *p1 = 'A';  // Write to page
  
  memstat(&info);
  found = 0;
  for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
    if(info.pages[i].state == RESIDENT && info.pages[i].va == (uint)(unsigned long)p1) {
      printf("After write: Page va=0x%x is_dirty=%d (should be 1)\n", 
             info.pages[i].va, info.pages[i].is_dirty);
      if(info.pages[i].is_dirty == 1) {
        printf("✓ Dirty bit correctly set\n");
      }
      found = 1;
      break;
    }
  }
  
  // Test 3: Allocate many pages, some clean, some dirty
  printf("\n--- Test 5c: Mixed clean and dirty pages ---\n");
  
  char *pages[50];
  for(int i = 0; i < 50; i++) {
    pages[i] = sbrk(4096);
    if(pages[i] == (char*)-1) {
      printf("sbrk failed at %d\n", i);
      break;
    }
    
    // Write to some pages (odd indices), leave others clean (even indices)
    if(i % 2 == 1) {
      pages[i][0] = (char)i;  // Dirty
    } else {
      char x = pages[i][0];   // Just read - clean
      (void)x;
    }
  }
  
  memstat(&info);
  printf("Allocated 50 pages, half clean, half dirty\n");
  
  int clean_count = 0;
  int dirty_count = 0;
  
  for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
    if(info.pages[i].state == RESIDENT) {
      if(info.pages[i].is_dirty) {
        dirty_count++;
      } else {
        clean_count++;
      }
    }
  }
  
  printf("Resident pages: %d clean, %d dirty\n", clean_count, dirty_count);
  
  // Test 4: Force page replacement to see eviction behavior
  printf("\n--- Test 5d: Page eviction behavior ---\n");
  printf("Allocating many more pages to trigger eviction...\n");
  
  for(int i = 50; i < 150; i++) {
    char *p = sbrk(4096);
    if(p == (char*)-1) {
      break;
    }
    
    // Make all new pages dirty
    p[0] = (char)i;
    
    if(i % 20 == 0) {
      memstat(&info);
      printf("Allocated %d: resident=%d swapped=%d\n",
             i, info.num_resident_pages, info.num_swapped_pages);
    }
  }
  
  memstat(&info);
  printf("\nFinal state:\n");
  printf("  Resident: %d\n", info.num_resident_pages);
  printf("  Swapped: %d\n", info.num_swapped_pages);
  printf("  Total: %d\n", info.num_pages_total);
  
  // Check dirty bits in swapped pages
  int swapped_clean = 0;
  int swapped_dirty = 0;
  
  for(int i = 0; i < info.num_pages_total && i < MAX_PAGES_INFO; i++) {
    if(info.pages[i].state == SWAPPED) {
      if(info.pages[i].is_dirty) {
        swapped_dirty++;
      } else {
        swapped_clean++;
      }
    }
  }
  
  printf("\nSwapped pages: %d clean, %d dirty\n", swapped_clean, swapped_dirty);
  printf("Note: Clean pages from executable may have been discarded\n");
  
  printf("\n=== DIRTY PAGE TRACKING TEST COMPLETE ===\n");
  
  exit(0);
}
