#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "../threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "../filesys/directory.h"
#include "threads/thread.h"


/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    // block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    bool is_dir;
    block_sector_t direct[123];
    block_sector_t indirect;
    block_sector_t doubly_indirect;
    unsigned magic;                     /* Magic number. */
    // uint32_t unused[125];               /* Not used. */
  };

struct indirect_inode 
  {
    block_sector_t blocks[128];
  };

block_sector_t 
get_block_sector (size_t index, struct inode_disk* block) 
{
  if (index < 123) 
    {
      return block->direct[index];
    } 
  else if (index < 123 + 128) 
    {
      struct indirect_inode* indirect_block = calloc (1, BLOCK_SECTOR_SIZE);
      block_read (fs_device, block->indirect, indirect_block);
      block_sector_t result = indirect_block->blocks[index - 123];
      free (indirect_block);
      return result;
    } 
  struct indirect_inode* doubly = calloc (1, BLOCK_SECTOR_SIZE);
  block_read (fs_device, block->doubly_indirect, doubly);
  struct indirect_inode* indirect_block = calloc (1, BLOCK_SECTOR_SIZE);
  block_read (fs_device, doubly->blocks[(index - 251) / 128], indirect_block);
  block_sector_t result = indirect_block->blocks[(index - 251) % 128];
  free (indirect_block);
  free (doubly);
  return result;
}


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock inode_lock;             /* Lock to protect metadata */
  };

bool 
inode_is_dir (struct inode *node)
{
  return node->data.is_dir;
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    {
      block_sector_t block_sector = get_block_sector(pos / BLOCK_SECTOR_SIZE, &inode->data);
      return block_sector;
    }
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
  lock_init (&open_inodes_lock);
}

/*
  extends a file num_blocks of zeros from start_block for the disk_inode
  which will be updated at block_sector_t sector.
*/
bool
inode_extend (size_t start_block, size_t num_blocks, struct inode_disk* disk_inode, block_sector_t sector) {
  if (num_blocks == 0) {
    return true;
  }
  // writing zeros
  struct indirect_inode* indirect = NULL;
  struct indirect_inode* doubly = NULL;
  struct indirect_inode* doubly_children[128];
  memset (doubly_children, 0, sizeof (doubly_children));
  static char zeros[BLOCK_SECTOR_SIZE];
  size_t i;
  for(i = start_block; i < num_blocks + start_block; i++)
    {
      if (i < 123) // direct
        if (free_map_allocate (1, &disk_inode->direct[i]))
          block_write (fs_device, disk_inode->direct[i], zeros);
        else 
          return false;
      else if (i < 251) // singly indirect
        {
          if (indirect == NULL) 
            {
              //single indirect block NOT ZEROS but sector nums
              indirect = calloc (1, sizeof (struct indirect_inode));
              if (disk_inode->indirect != NULL)
                block_read (fs_device, disk_inode->indirect, indirect);
              else if (!free_map_allocate (1, &disk_inode->indirect))
                return false;
            }
          if (free_map_allocate (1, &indirect->blocks[i - 123]))
            block_write (fs_device, indirect->blocks[i - 123], zeros);
          else
            return false;
        } 
      else 
        {
          if (doubly == NULL) 
            {
              //doubly indirect block NOT ZEROS but sector nums
              doubly = calloc (1, sizeof(struct indirect_inode));
              if (disk_inode->doubly_indirect != NULL) 
                block_read (fs_device, disk_inode->doubly_indirect, doubly);
              else if (!free_map_allocate (1, &disk_inode->doubly_indirect))
                return false;
            }
          int l1_index = (i - 251) / 128;
          int l2_index = (i - 251) % 128;
          if (doubly_children[l1_index] == NULL) 
            {
              //doubly indirect child block NOT ZEROS but sector nums
              doubly_children[l1_index] = calloc (1, sizeof(struct indirect_inode));
              if (doubly->blocks[l1_index] != NULL)
                block_read (fs_device, doubly->blocks[l1_index], doubly_children[l1_index]);
              else if (!free_map_allocate (1, &doubly->blocks[l1_index]))
                return false;
            }
          if (free_map_allocate (1, &doubly_children[l1_index]->blocks[l2_index]))
            block_write (fs_device, doubly_children[l1_index]->blocks[l2_index], zeros);
          else 
            return false;
        }
    }
  // write indirect blocks
  if (indirect != NULL) 
    {
      block_write (fs_device, disk_inode->indirect, indirect);
      free (indirect);
    }
  if (doubly != NULL) 
    {
      block_write (fs_device, disk_inode->doubly_indirect, doubly);
      size_t i;
      int start = (start_block < 251) ? 0 : (start_block - 251) / 128;
      for (i = start; i < ((start_block + num_blocks - 251) / 128) + 1; i++)
        {
          block_write (fs_device, doubly->blocks[i], doubly_children[i]);
          free (doubly_children[i]);
        }
      free(doubly);
    }
  // writing inode
  block_write (fs_device, sector, disk_inode);
  return true;

}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = (length) ? bytes_to_sectors (length) : 1;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      bool result = inode_extend (0, sectors, disk_inode, sector);
      free (disk_inode);
      return result;
    }
  
  return false;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire (&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          lock_release (&open_inodes_lock);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    lock_release (&open_inodes_lock);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release (&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->inode_lock);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    lock_acquire (&inode->inode_lock);
    inode->open_cnt++;
    lock_release (&inode->inode_lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  lock_acquire (&inode->inode_lock);

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      lock_acquire (&open_inodes_lock);
      list_remove (&inode->elem);
      lock_release (&open_inodes_lock);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release (inode->sector, 1);
          size_t i;
          for(i = 0; i < bytes_to_sectors(inode->data.length); i++)
            {
              free_map_release (get_block_sector(i, &inode->data), 1);
            }
          // free_map_release (inode->data.start,
          //                   bytes_to_sectors (inode->data.length));
        }
      lock_release (&inode->inode_lock);
      free (inode);
    }
  else
      lock_release (&inode->inode_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;
  lock_acquire (&inode->inode_lock);
  if (inode->deny_write_cnt)
    {
      lock_release (&inode->inode_lock);
      return 0;
    }
  lock_release (&inode->inode_lock);
  size_t write_blocks = bytes_to_sectors(offset + size);
  size_t current_blocks = bytes_to_sectors(inode_length(inode));
  
  if (write_blocks > current_blocks) {
    size_t length = write_blocks - current_blocks;
    inode_extend (current_blocks, length, &inode->data, inode->sector);
  }
  if (offset + size > inode_length(inode)) {
    inode->data.length = offset + size;
    block_write (fs_device, inode->sector, &inode->data);
  }
  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  lock_acquire (&inode->inode_lock);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  lock_release (&inode->inode_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  lock_acquire (&inode->inode_lock);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  lock_release (&inode->inode_lock);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

void 
set_dir (struct inode *inode)
{
  inode->data.is_dir = true;
  block_write (fs_device, inode->sector, &inode->data);
}
bool
is_relative (char *path)
{
  if (*path == '/')
    return false;
  return true;
}
/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
next call will return the next file name part. Returns 1 if successful, 0 at
end of string, -1 for a too-long file name part. */
int
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

char*
get_last_part(char* path) 
  { 
    char part[NAME_MAX + 1];
    while (get_next_part (part, &path)){}
    return part;
  }


struct dir* get_parent_dir_from_path (char* path) 
  {
    char part[NAME_MAX + 1];
    struct dir *curr_dir = (is_relative (path) && thread_current ()->working_dir) ? dir_reopen (thread_current ()->working_dir) : dir_open_root();
    struct inode *inode_next = dir_get_inode (curr_dir);
    // struct inode *inode_parent = NULL;
    struct dir* parent_dir = NULL;

    while (curr_dir && get_next_part (part, &path)) 
      {
        dir_close (parent_dir);
        parent_dir = curr_dir;
        if (dir_lookup (curr_dir, part, &inode_next))
          if (inode_is_dir (inode_next))
            curr_dir = dir_open (inode_next); // parent != curr
          else {
            inode_close (inode_next);
            curr_dir = NULL; //parent = curr
          }
        else
          curr_dir = NULL; //parent = curr
      }
    if (!get_next_part (part, &path)) {
      dir_close (curr_dir);
      return parent_dir;
    }
    dir_close (curr_dir);
    dir_close (parent_dir);
    return NULL;
  }

struct inode* 
get_inode_from_path (char* path) 
{
  char part[NAME_MAX + 1];
  struct dir *curr_dir = (is_relative (path) && thread_current ()->working_dir) ? dir_reopen (thread_current ()->working_dir) : dir_open_root();
  struct inode *inode_next = dir_get_inode (curr_dir);

  while (curr_dir && get_next_part (part, &path)) 
    {
      if (inode_is_removed (dir_get_inode (curr_dir)))
      {
        dir_close (curr_dir);
        return NULL;
      }
      if (dir_lookup (curr_dir, part, &inode_next))
      {
        dir_close (curr_dir);
        if (inode_is_dir (inode_next))
          curr_dir = dir_open (inode_next);
        else {
          curr_dir = NULL;
        }
      }
      else
      {
        dir_close (curr_dir);
        curr_dir = NULL;
      }
    }
  return inode_next;
}

bool 
inode_is_removed (struct inode* inode) 
  {
    return inode->removed;
  }
