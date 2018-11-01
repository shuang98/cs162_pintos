#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_pointer (void* ptr) {
  return is_user_vaddr(ptr) && pagedir_get_page (thread_current()->pagedir, ptr);
}
static void
invalid_access (struct intr_frame *f UNUSED) {
  f->eax = -1;
  printf("%s: exit(%d)\n", &thread_current ()->name, -1);
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  // printf("System call number: %d\n", args[0]);
  if (is_valid_pointer(f->esp) && is_valid_pointer(args + 4)) {
    if (args[0] == SYS_EXIT) {
      f->eax = args[1];
      printf("%s: exit(%d)\n", &thread_current ()->name, args[1]);
      thread_exit ();
    } else if (args[0] == SYS_WRITE) {
      int fd = args[1];
      // probably need to check args for this call too.
      char *buffer = (char*)args[2];
      size_t size = args[3];
      write (fd, buffer, size);
    } else if (args[0] == SYS_PRACTICE) {
      f->eax = args[1] + 1;
    } else if (args[0] == SYS_WAIT) {
      
    } else if (args[0] == SYS_EXEC) {
    }
  } else {
    invalid_access (f);
  }
}
