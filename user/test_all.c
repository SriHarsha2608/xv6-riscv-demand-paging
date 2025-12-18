#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Master test runner for all demand paging tests
int
main(int argc, char *argv[])
{
  printf("\n");
  printf("========================================\n");
  printf("  DEMAND PAGING COMPREHENSIVE TEST SUITE\n");
  printf("========================================\n");
  printf("\n");
  printf("This suite tests all aspects of the PagedOut Inc. system:\n");
  printf("  1. Lazy allocation\n");
  printf("  2. FIFO page replacement\n");
  printf("  3. Swap file operations\n");
  printf("  4. Invalid access detection\n");
  printf("  5. Dirty page tracking\n");
  printf("  6. Swap capacity limits\n");
  printf("  7. Fork and swap isolation\n");
  printf("\n");
  printf("Note: Check kernel console logs for detailed operation logs\n");
  printf("      (PAGEFAULT, ALLOC, RESIDENT, MEMFULL, VICTIM, etc.)\n");
  printf("\n");
  
  char *tests[] = {
    "test_lazy",
    "test_fifo", 
    "test_swap",
    "test_invalid",
    "test_dirty",
    "test_swapfull",
    "test_fork",
    0
  };
  
  int total = 0;
  int passed = 0;
  
  for(int i = 0; tests[i] != 0; i++) {
    total++;
    printf("========================================\n");
    printf("Running: %s (Test %d)\n", tests[i], i+1);
    printf("========================================\n");
    
    int pid = fork();
    if(pid == 0) {
      // Child runs the test
      char *args[2];
      args[0] = tests[i];
      args[1] = 0;
      exec(tests[i], args);
      
      // If exec fails
      printf("ERROR: Failed to exec %s\n", tests[i]);
      exit(1);
    } else if(pid > 0) {
      // Parent waits
      int status;
      wait(&status);
      
      if(status == 0) {
        printf("\n✓ %s PASSED\n", tests[i]);
        passed++;
      } else {
        printf("\n✗ %s FAILED (exit status: %d)\n", tests[i], status);
      }
    } else {
      printf("ERROR: fork failed for %s\n", tests[i]);
    }
    
    printf("\n");
  }
  
  printf("========================================\n");
  printf("  TEST SUMMARY\n");
  printf("========================================\n");
  printf("Total tests: %d\n", total);
  printf("Passed: %d\n", passed);
  printf("Failed: %d\n", total - passed);
  printf("\n");
  
  if(passed == total) {
    printf("🎉 ALL TESTS PASSED! 🎉\n");
    printf("\nYour demand paging implementation is working correctly!\n");
  } else {
    printf("⚠ SOME TESTS FAILED\n");
    printf("\nPlease review the kernel logs and fix the failing tests.\n");
  }
  
  printf("\n");
  
  exit(passed == total ? 0 : 1);
}
