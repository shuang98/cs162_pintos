Design Document for Project 3: File System
==========================================

## Group Members

* Eric Li <eric.li5009@berkeley.edu>
* Stanley Huang <shuang1998@berkeley.edu>
* Kevin Liu <kevinliu36@berkeley.edu>
* Ayush Maganahalli <ayush.sm@berkeley.edu>

## Task 1: Buffer Cache
### Data structures and functions
 We will create a new `cache_block` struct in `inode.c` which will represent a block of our fully associative cache. The cache itself will be a linked list of these `cache_block` structs of max size 64, called `buffer_cache`.
 
```c
static struct list buffer_cache;


struct cache_block {
    block_sector_t sector;
    bit dirty;
    struct list_elem elem;
    struct lock lock;
    char data[512];
}
```
We will remove the data attribute from the existing `struct inode` as all data accesses will be handled through our cache.
```c

struct inode
  {
    struct list_elem elem;
    block_sector_t sector;
    int open_cnt;                      
    bool removed;                       
    int deny_write_cnt;                 
  };
  
```
**Function Changes:**
* Make `inode_open` implement LRU replacement strategy
* Make `inode_read_at` and `inode_write_at` put data directly into the buffer cache rather than bounce buffer
* Make sure buffer cache blocks get written to disk when `filesys_done` is invoked

### Algorithms
**Disk Access with Cache:**
- Iterate through our `buffer_cache` list and search for a matching sector number within each block
	- Acquire `cache_block->lock`
	- If there is a match read from matching `cache_block->data` 
	- If no match then `block_read()` to retrieve block from disk and either put it into the first free `cache_block` or evict a `cache_block` to make room
	- If the operation is a write then set `dirty` to 1 and write to `cache_block->data`.
	- Release lock when done.

**Eviction/Replacement Policy:**
- We will be implementing an LRU replacement policy for our buffer cache.
	- The most recently used cache block will be moved to the end of the `buffer_cache` list.
	- Whenever we need to evict we will always evict the first element of `buffer_cache`
- We will use `block_write` to flush data when evicting a cache block that is dirty. 

### Synchronization
 - There is a race condition where one process tries to access a `sector` that is currently being evicted in the cache
     - If the eviction happens before the process accessing the sector acquires the lock, the data access operation needs to be restarted. (Most likely will be using loop with data access)
### Rationale
 We decided to use a linked list as our `buffer_cache` since it makes implementing LRU very easy.

## Task 2: Extensible Files
### Data structures and functions
We modify the `inode_disk` struct in `inode.c` to implement direct, indirect, and doubly indirect index structure.
```c
struct inode_disk {
	off_t length;
    unsigned magic;
    block_sector_t direct[124];
    block_sector_t indirect;
    block_sector_t doubly_indirect;
}
```
We create a new `indirect_inode` struct to serve as our indirect nodes.
```c
struct indirect_disk {
	unsigned magic;
    block_sector_t blocks[127];
}
```
We also modify `inode_write_at` and `inote_read_at` to support file growth.
```c
off_t inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset);
off_t inode_write_at (struct inode *inode, void *buffer_, off_t size, off_t offset);
```
Finally we implement a new syscall `inumber` in `syscall.c`.
```c
int inumber(int fd);
```
### Algorithms
**inode Changes:**
We modify `byte_to_sector` to calculate how to partition the offset using direct, indirect, and doubly indirect pointers. Then we get the sector number for the offset by following inode pointers.

**File Growth:**
* if `byte_to_sector` fails, return 0 bytes written
* if file position is past EOF, write zeros to fill the space, updating `inode_disk` to point to new blocks as needed

**Read:**
* if `byte_to_sector` fails, return 0 bytes read

**inumber Syscall:**
* look up file in file descriptor table
* get corresponding inode
* return corresponding sector number
### Synchronization
Synchronization is handled by the cache, since all file system operations go through the cache.
### Rationale
We followed the recommended implementation using direct, indirect and doubly indirect pointer structure. For writing past the end of a file, we allocate immediately and zero out the memory. This is much simpler than waiting for a file actually write to that new memory as we don't have to track any information. If we don't have enough space, we don't write anything. This is easier than having to reset to a state before we started writing.

## Task 3: Subdirectories
### Data structures and functions
 We will add a `bool is_dir` field to `struct inode_disk`. This allows us to distinguish files and directories. (Note our `block_sector_t direct[124]` will need to be reduced to size 123 to fit new field)
```c
//inode.c
struct inode_disk {
    ...
    bool is_dir;
    ...
}
```

We will give `struct thread` to add a pointer to the string stating the current directory. This field will be initialized with a value of `"/"`. Child threads will inherit the current directory of its direct parent.
```c
struct thread {
    ...
    char* working_dir;
    ...
}
```
We will also add the appropriate functions for the new syscalls:
```c
static bool chdir (const char *dir)
static bool mkdir (const char *dir)
static bool readdir (int fd, char *name)
static bool isdir (int fd)
```
### Algorithms
 **Change Directory:**
 - To check if the file exists we will invoke `filesys_open`
 - If the file opened with no errors we can update the `working_dir` field in `struct thread` accordingly
 
 **Make Directory:**
 - Check if file exists using `filesys_open` and then close it if successfully opened
 	- If the file exists return `False` else continue:
 - Use `dir_create (block_sector_t sector, size_t entry_cnt)`, with a `sector` retrieved from the free map and an initial `entry_cnt` as 1
 
 **Read Directory:**
 - Check if file exists using `filesys_open` and then close it if successfully opened that the file exists. If doesn't exist return `False`
 - Use `dir_readdir (struct dir *dir, char name[NAME_MAX + 1])` with a buffer and a `struct dir* dir` that can be obtained through `int fd` and the file descriptor table.
 
 **Is Directory:**
  - Check if file exists using `filesys_open` and then close it if successfully opened that the file exists. If doesn't exist return `False`
  - Find the `file` from the fd table and return its `inode->is_dir` 
 
 We will also need to edit current file syscalls like open, close, etc. to consider directories as well.
 
 **Processing File Names:**
     - We first need to discern whether or not it is an absolute or relative path. We concatenate our input file name to our working directory if the path is relative
     - We then iterate using `dir_lookup` to find the next file/directory in the path until we reach the end
     - We can skip `.` paths, since they don't affect the file location
     - For `..` paths, scan ahead through the string for any instances of `..` and delete that instance and the directory before it. 
### Synchronization
 No data races, synch will mostly be handled with cache.
### Rationale
 Our implementation is pretty straight forward. We decided to add the `is_dir` to our inode for an easy file/directory identifier. We also decided to add a `working_dir` field to the thread struct as each process will have its own working directory. Having access to the working directory makes processing relative paths a lot easier. 





## Additional Questions
In order to use a buffer cache with write-behind we want to ensure that we periodically flush the dirty blocks (blocks that have their data  changed). As a result, we can have an interrupt call to write to disk after an arbitrary amount of time (e.g. 15 seconds or a minute) that gives us the frequency with which the block is written to main memory. This ensures that memory always has a relatively updated version of the data in the case of a system failure. In order to use this method, we need to have a corresponding lock for a block of memory in the cache such that the interrupt call can acquire the lock in order to prevent writes to the data while the data is being transferred to memory and not cause cache incoherence. This whole method can be simply achieved by having a single thread run in a permanent while loop that writes when a timer reaches the cycle time for a dirty block write.

In contrast, for a buffer cache with read-ahead, we want to predict what the next block might be. As a result, since we know that we have some number of sectors to read (can be 0, we're just checking if there's anything to do), then we allocate a list for that list of sectors we're going to read. On top of that, we have a corresponding lock that exists in a file. By doing this when the queue of sectors to be read is not empty, then we simply acquire the lock and pop off the first sector in the queue, after which we release the lock and read the sector. By doing this, we ensure that we only have that sector to read and it allows us to methodically go through the queue. Next, when we do end up in situations that we read or write to inodes, we just add the corresponding sector for the inode's file to the queue so that we eventually end up reading that sector. Lastly, since we also do this dynamically in memory, we malloc whenever we add to the queue and free whenever we pop off to read.
