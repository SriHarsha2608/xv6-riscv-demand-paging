#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/memstat.h"
#include "user/user.h"

#define PAGES_PER_CHILD  500    // allocate 500 pages per child (under 1024 limit)
#define CHILDREN          3

static void dirty_pages(char *base, int npages) {
  for (int i = 0; i < npages; i++) {
    base[i * 4096] = (char)(i & 0xFF);
  }
}

int
main(void)
{
  int pid;
  struct proc_mem_stat st;

  printf("swapstress: start\n");

  for (int c = 0; c < CHILDREN; c++) {
    pid = fork();
    if (pid == 0) {
      // child
      int bytes = PAGES_PER_CHILD * 4096;
      char *start = sbrklazy(bytes);
      if (start == (char*)-1) {
        printf("swapstress child: sbrk failed\n");
        exit(1);
      }
      dirty_pages(start, PAGES_PER_CHILD);
      if (memstat(&st) == 0) {
        printf("child %d: resident=%d swapped=%d total=%d next_seq=%d\n",
               getpid(), st.num_resident_pages, st.num_swapped_pages,
               st.num_pages_total, st.next_fifo_seq);
      }
      pause(10);
      exit(0);
    }
  }

  // parent: monitor
  for (int i = 0; i < 20; i++) {
    if (memstat(&st) == 0) {
      printf("parent %d: resident=%d swapped=%d total=%d next_seq=%d\n",
             getpid(), st.num_resident_pages, st.num_swapped_pages,
             st.num_pages_total, st.next_fifo_seq);
    }
    pause(5);
  }

  // reap
  for (int c = 0; c < CHILDREN; c++) {
    wait(0);
  }
  printf("swapstress: done\n");
     exit(0);
}
