#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_pointer (void* ptr)
{
  return is_user_vaddr(ptr + 3) && pagedir_get_page (thread_current()->pagedir, ptr);
}

static void
invalid_access (struct intr_frame *f UNUSED)
{
  f->eax = -1;
  thread_current()->parent_wait->exit_code = -1;
  printf("%s: exit(%d)\n", &thread_current ()->name, -1);
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
  // printf("System call number: %d\n", args[0]);
  if (!is_valid_pointer (f->esp) || !is_valid_pointer(f->esp + 4))
    invalid_access (f);
  enum intr_level old;
  switch (args[0])
    {
      case SYS_READ:
      case SYS_WRITE:
        {
          if (!is_valid_pointer (f->esp + 12))
            invalid_access (f);
          int fd = args[1];
          char *buffer = (char *) args[2];
          if (buffer == NULL || !is_valid_pointer (buffer))
            invalid_access (f);
          uint32_t size = args[3];
          lock_acquire (&filesys_lock);
          int result;
          if (args[0] == SYS_READ)
            {
              result = read (fd, buffer, size);
            }
          else
            {
              result = write (fd, buffer, size);
            }
          lock_release (&filesys_lock);
          if (result == -1)
            invalid_access (f);
          f->eax = result;
          break;
        }

      case SYS_SEEK:
      case SYS_CREATE:
        {
          if (!is_valid_pointer (f->esp + 8))
            invalid_access (f);
          if (args[0] == SYS_SEEK)
            {
              int fd = args[1];
              uint32_t pos = args[2];
              lock_acquire (&filesys_lock);
              seek (fd, pos);
              lock_release (&filesys_lock);
            }
          else if (args[0] == SYS_CREATE)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              uint32_t init_size = args[2];
              lock_acquire (&filesys_lock);
              bool result = create (file, init_size);
              lock_release (&filesys_lock);
              f->eax = result;
            }
          break;
        }

      case SYS_OPEN:
      case SYS_FILESIZE:
      case SYS_TELL:
      case SYS_CLOSE:
      case SYS_REMOVE:
        {
          if (!is_valid_pointer (f->esp + 4))
            invalid_access (f);
          if (args[0] == SYS_REMOVE)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              lock_acquire (&filesys_lock);
              bool result = remove (file);
              lock_release (&filesys_lock);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_OPEN)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              lock_acquire (&filesys_lock);
              int result = open (file);
              lock_release (&filesys_lock);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_FILESIZE)
            {
              int fd = args[1];
              lock_acquire (&filesys_lock);
              int result = filesize (fd);
              lock_release (&filesys_lock);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_TELL)
            {
              int fd = args[1];
              lock_acquire (&filesys_lock);
              unsigned result = tell (fd);
              lock_release (&filesys_lock);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_CLOSE)
            {
              int fd = args[1];
              close (fd);
              break;
            }
          else if (args[0] == SYS_EXIT)
            {
              f->eax = args[1];
              printf("%s: exit(%d)\n", &thread_current ()->name, args[1]);
              thread_exit();
              break;
            }
        }
      case SYS_EXIT:
        old = intr_disable();
        f->eax = args[1];
        thread_current()->parent_wait->exit_code = args[1];
        printf("%s: exit(%d)\n", &thread_current ()->name, args[1]);
        thread_exit ();
        intr_set_level(old);
        break;
      case SYS_WAIT:
        f->eax = process_wait(args[1]);
        break;
      case SYS_EXEC:
        old = intr_disable();
        if (!is_valid_pointer(args[1])) {
          invalid_access(f);
          return NULL;
        }
        int child_id = process_execute((char*) args[1]);
        struct thread* child = thread_by_id(child_id);
        intr_set_level(old);
        sema_down(&child->parent_wait->load_semaphore);
        if (list_empty(&thread_current()->child_waits)) {
          f->eax = -1;
          return NULL;
        }
        struct wait_status* w;
        struct list_elem* e = list_front(&thread_current()->child_waits);
        while (e != list_tail(&thread_current()->child_waits)) {
          w = list_entry(e, struct wait_status, elem);
          if (w->child_id == child_id) {
            break;
          }
          e = list_next(e);
        }
        if (!w->successfully_loaded) {
          f->eax = -1;
        } else {
          f->eax = child_id;
        }
        break;
      case SYS_PRACTICE:
        f->eax = args[1] + 1;
        break;

      case SYS_HALT:
        {
          // shutdown_power_off ();
          break;
        }
    }
}
