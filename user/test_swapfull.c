#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test swap capacity limits (1024 pages max)
int
main(int argc, char *argv[])
{
  printf("=== TEST 6: SWAP CAPACITY LIMITS ===\n");
  printf("Max swap capacity: 1024 pages (4 MB)\n");
  
  struct proc_mem_stat info;
  
  // Test 1: Allocate well within swap limits
  printf("\n--- Test 6a: Within swap limits ---\n");
  printf("Allocating 500 pages...\n");
  
  char *pages[500];
  int allocated = 0;
  
  for(int i = 0; i < 500; i++) {
    pages[i] = sbrk(4096);
    if(pages[i] == (char*)-1) {
      printf("sbrk failed at %d\n", i);
      break;
    }
    allocated++;
    
    // Write to make dirty (forces swap-out, not discard)
    for(int j = 0; j < 100; j++) {
      pages[i][j * 40] = (char)(i + j);
    }
    
    if(i % 50 == 0 && i > 0) {
      memstat(&info);
      printf("  Progress %d: resident=%d swapped=%d\n",
             i, info.num_resident_pages, info.num_swapped_pages);
    }
  }
  
  memstat(&info);
  printf("\nAfter 500 pages:\n");
  printf("  Allocated: %d\n", allocated);
  printf("  Resident: %d\n", info.num_resident_pages);
  printf("  Swapped: %d\n", info.num_swapped_pages);
  printf("  Total: %d\n", info.num_pages_total);
  
  if(allocated == 500) {
    printf("✓ Successfully allocated within swap limits\n");
  }
  
  // Test 2: Try to exceed swap capacity in child process
  printf("\n--- Test 6b: Exceeding swap capacity ---\n");
  
  int pid = fork();
  if(pid == 0) {
    // Child: try to allocate more than swap can hold
    printf("Child: Attempting to allocate 1500 pages (exceeds 1024 limit)...\n");
    
    char *bigpages[1500];
    int child_allocated = 0;
    
    for(int i = 0; i < 1500; i++) {
      bigpages[i] = sbrk(4096);
      if(bigpages[i] == (char*)-1) {
        printf("Child: sbrk failed at %d pages\n", i);
        break;
      }
      child_allocated++;
      
      // Make every page dirty to force swap writes
      for(int j = 0; j < 256; j++) {
        bigpages[i][j * 16] = (char)(i & 0xFF);
      }
      
      if(i % 100 == 0 && i > 0) {
        if(memstat(&info) == 0) {
          printf("Child progress %d: resident=%d swapped=%d\n",
                 i, info.num_resident_pages, info.num_swapped_pages);
        }
      }
      
      // Check if we're approaching swap limit
      if(memstat(&info) == 0 && info.num_swapped_pages > 1000) {
        printf("Child: Swapped pages = %d (approaching 1024 limit)\n", 
               info.num_swapped_pages);
      }
    }
    
    printf("Child: Allocated %d pages before termination\n", child_allocated);
    exit(0);
  } else {
    // Parent waits for child
    int status;
    wait(&status);
    
    if(status != 0) {
      printf("✓ Child terminated (likely due to swap exhaustion)\n");
      printf("  This is expected when swap capacity is exceeded\n");
    } else {
      printf("Child completed without error\n");
      printf("  Either swap limit wasn't reached or system has more memory\n");
    }
  }
  
  // Test 3: Verify parent is still okay
  printf("\n--- Test 6c: Parent process integrity ---\n");
  memstat(&info);
  printf("Parent still running:\n");
  printf("  Resident: %d\n", info.num_resident_pages);
  printf("  Swapped: %d\n", info.num_swapped_pages);
  
  // Verify parent's data is still intact
  printf("Verifying parent's data integrity...\n");
  int errors = 0;
  for(int i = 0; i < allocated && i < 500; i++) {
    if(pages[i] != (char*)-1) {
      char expected = (char)(i & 0xFF);
      if(pages[i][0] != expected) {
        errors++;
      }
    }
  }
  
  if(errors == 0) {
    printf("✓ Parent's data intact after child termination\n");
  } else {
    printf("FAIL: Parent's data corrupted (%d errors)\n", errors);
  }
  
  printf("\n=== SWAP CAPACITY TEST COMPLETE ===\n");
  
  exit(0);
}
