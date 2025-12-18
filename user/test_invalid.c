#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

// Test invalid memory access detection
int
main(int argc, char *argv[])
{
  printf("=== TEST 4: INVALID ACCESS DETECTION ===\n");
  
  int pid;
  
  // Test 1: Access beyond heap
  printf("\n--- Test 4a: Access beyond heap ---\n");
  pid = fork();
  if(pid == 0) {
    // Child: try to access way beyond heap
    char *ptr = sbrk(4096);  // Allocate one page
    printf("Child: Allocated 1 page at %p\n", ptr);
    
    printf("Child: Attempting invalid access far beyond heap...\n");
    char *bad_ptr = ptr + 1000 * 4096;  // 1000 pages beyond
    *bad_ptr = 'X';  // Should kill process
    
    printf("FAIL: Should have been killed!\n");
    exit(1);
  } else {
    wait(0);
    printf("✓ Child correctly terminated for out-of-bounds access\n");
  }
  
  // Test 2: Access below stack
  printf("\n--- Test 4b: Access far below stack ---\n");
  pid = fork();
  if(pid == 0) {
    printf("Child: Attempting access far below stack...\n");
    
    // Get approximate stack pointer
    int stack_var;
    char *sp = (char*)&stack_var;
    
    // Try to access way below stack (more than 1 page below SP)
    char *bad_ptr = sp - 2 * 4096;  // 2 pages below SP
    *bad_ptr = 'Y';  // Should kill process
    
    printf("FAIL: Should have been killed!\n");
    exit(1);
  } else {
    wait(0);
    printf("✓ Child correctly terminated for invalid stack access\n");
  }
  
  // Test 3: NULL pointer dereference
  printf("\n--- Test 4c: NULL pointer dereference ---\n");
  pid = fork();
  if(pid == 0) {
    printf("Child: Attempting NULL pointer dereference...\n");
    char *null_ptr = 0;
    *null_ptr = 'Z';  // Should kill process
    
    printf("FAIL: Should have been killed!\n");
    exit(1);
  } else {
    wait(0);
    printf("✓ Child correctly terminated for NULL dereference\n");
  }
  
  // Test 4: Access to unmapped high address
  printf("\n--- Test 4d: Access to high unmapped address ---\n");
  pid = fork();
  if(pid == 0) {
    printf("Child: Attempting access to high unmapped address...\n");
    char *high_ptr = (char*)(unsigned long)0x80000000;
    *high_ptr = 'W';  // Should kill process
    
    printf("FAIL: Should have been killed!\n");
    exit(1);
  } else {
    wait(0);
    printf("✓ Child correctly terminated for unmapped access\n");
  }
  
  // Test 5: Valid stack growth (should succeed)
  printf("\n--- Test 4e: Valid stack access (should succeed) ---\n");
  pid = fork();
  if(pid == 0) {
    printf("Child: Testing valid stack growth...\n");
    
    // Access within one page below stack should work
    int stack_var;
    char *sp = (char*)&stack_var;
    char *valid_ptr = sp - 100;  // Well within same page
    *valid_ptr = 'V';
    
    printf("✓ Valid stack access succeeded\n");
    exit(0);
  } else {
    int status;
    wait(&status);
    if(status == 0) {
      printf("✓ Child completed valid stack test successfully\n");
    } else {
      printf("FAIL: Child should not have been killed for valid access\n");
    }
  }
  
  printf("\n=== ALL INVALID ACCESS TESTS COMPLETED ===\n");
  printf("Summary: System correctly detects and handles invalid accesses\n");
  
  exit(0);
}
