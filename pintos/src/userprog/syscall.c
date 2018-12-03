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
#include "../filesys/directory.h"
#include "../filesys/filesys.h"
#include "../filesys/inode.h"


static void syscall_handler (struct intr_frame *);
static bool is_relative (char *path);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
is_valid_pointer (void* ptr)
{
  return is_user_vaddr (ptr + 3) && pagedir_get_page (thread_current()->pagedir, ptr);
}

static void
invalid_access (struct intr_frame *f UNUSED)
{
  f->eax = -1;
  thread_current ()->parent_wait->exit_code = -1;
  printf ("%s: exit(%d)\n", &thread_current ()->name, -1);
  thread_exit ();
}

static bool
is_relative (char *path)
{
  if (*path == '/')
    return false;
  return true;
}

/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) 
{
  const char *src = *srcp;
  char *dst = part;
  
  /* Skip leading slashes. If it’s all slashes, we’re done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;
  
  /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';
  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  uint32_t* args = ((uint32_t*) f->esp);
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
          int result;
          if (args[0] == SYS_READ)
            {
              result = read (fd, buffer, size);
            }
          else
            {
              result = write (fd, buffer, size);
            }
          if (result == -1)
            invalid_access (f);
          f->eax = result;
          break;
        }

      case SYS_SEEK:
      case SYS_READDIR:
      case SYS_CREATE:
        {
          if (!is_valid_pointer (f->esp + 8))
            invalid_access (f);
          if (args[0] == SYS_SEEK)
            {
              int fd = args[1];
              uint32_t pos = args[2];
              seek (fd, pos);
            }
          else if (args[0] == SYS_CREATE)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              uint32_t init_size = args[2];
              bool result = create (file, init_size);
              f->eax = result;
            }
          else if (args[0] == SYS_READDIR)
            {
              // struct file *valid = filesys_open (args[2]);
              // if (valid == NULL)
              //   f->eax = 0;
              // filesys_remove (args[2]);
              // struct list_elem *e;
              // struct dir *dir;
              // struct thread *curr_thread = thread_current ();
              // for (e = list_begin (curr_thread->fd_root); e != list_end (curr_thread->fd_root); e = list_next (e))
              //   {
              //     struct fd_elem *f = list_entry (e, struct fd_elem, table_elem);
              //     if (f->fd == args[1])
              //       {
              //       }
              //   }
            }
          break;
        }

      case SYS_OPEN:
      case SYS_FILESIZE:
      case SYS_TELL:
      case SYS_CLOSE:
      case SYS_REMOVE:
      case SYS_INUMBER:
      case SYS_CHDIR:
      case SYS_MKDIR:      
      case SYS_ISDIR:
        {
          if (!is_valid_pointer (f->esp + 4))
            invalid_access (f);
          if (args[0] == SYS_REMOVE)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              bool result = remove (file);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_OPEN)
            {
              char *file = (char *) args[1];
              if (file == NULL || !is_valid_pointer (file))
                invalid_access (f);
              int result = open (file);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_FILESIZE)
            {
              int fd = args[1];
              int result = filesize (fd);
              f->eax = result;
              break;
            }
          else if (args[0] == SYS_TELL)
            {
              int fd = args[1];
              unsigned result = tell (fd);
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
              printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
              thread_exit();
              break;
            }
          else if (args[0] == SYS_INUMBER)
            {
              int fd = args[1];
              f->eax = inumber (fd);
              break;
            }
          else if (args[0] == SYS_CHDIR)
            {

            }
          else if (args[0] == SYS_MKDIR)
            {
              if (strlen (args[1]) == 0)
                {
                  f->eax = 0;
                  break;
                }
              char part[NAME_MAX + 1];
              if (is_relative (args[1]))
                {
                  struct dir *curr_dir = thread_current ()->working_dir;
                  struct inode *inode_next;
                  int check = 1;
                  while (get_next_part (part, &args[1]))
                    {
                      if (dir_lookup (curr_dir, part, &inode_next))
                        {
                          if (is_dir_inode (inode_next))
                            {
                              curr_dir = dir_open (inode_next);
                              inode_close (inode_next);
                            }
                          else
                            {
                              f->eax = 0;
                              return;
                            }
                        }
                      else
                        {
                          check = get_next_part (part, &args[1]);
                          break;
                        }
                    }
                  if (check == 0)
                    {
                      block_sector_t sector;
                      if (!free_map_allocate (1, &sector))
                        {
                          f->eax = 0;
                          return;
                        }
                      dir_add (curr_dir, part, sector);
                      dir_create (sector, 2);
                      struct inode *used;
                      dir_lookup (curr_dir, part, &used);
                      struct dir *new_dir = dir_open (used);
                      char *str1 = ".";
                      char *str2 = "..";
                      dir_add (new_dir, str1, sector);
                      struct inode *parent_inode = dir_get_inode (curr_dir);
                      dir_add (new_dir, str2, inode_get_inumber (parent_inode));
                      inode_close (used);
                    }
                  f->eax = 1;
                }
              else
                {
                }
              break;
            }
          else if (args[0] == SYS_ISDIR)
            {
              int fd = args[1];
              f->eax = is_dir (fd);
              break;
            }
        }
      case SYS_EXIT:
        old = intr_disable ();
        f->eax = args[1];
        thread_current ()->parent_wait->exit_code = args[1];
        printf ("%s: exit(%d)\n", &thread_current ()->name, args[1]);
        thread_exit ();
        intr_set_level (old);
        break;
      case SYS_WAIT:
        f->eax = process_wait (args[1]);
        break;
      case SYS_EXEC:
        old = intr_disable ();
        if (!is_valid_pointer (args[1])) 
          {
            invalid_access (f);
            return NULL;
          }
        int child_id = process_execute ((char*) args[1]);
        struct thread* child = thread_by_id (child_id);
        intr_set_level (old);
        sema_down (&child->parent_wait->load_semaphore);
        if (list_empty (&thread_current ()->child_waits)) 
          {
            f->eax = -1;
            return NULL;
          }
        struct wait_status* w;
        struct list_elem* e = list_front (&thread_current ()->child_waits);
        while (e != list_tail (&thread_current ()->child_waits)) 
          {
            w = list_entry (e, struct wait_status, elem);
            if (w->child_id == child_id) 
              {
                break;
              }
            e = list_next (e);
          }
        if (!w->successfully_loaded) 
          {
            f->eax = -1;
          } 
        else 
          {
            f->eax = child_id;
          }
        break;
      case SYS_PRACTICE:
        f->eax = args[1] + 1;
        break;

      case SYS_HALT:
        {
          shutdown_power_off ();
          break;
        }
    }
}
