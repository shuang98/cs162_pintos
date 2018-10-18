Design Document for Project 2: User Programs
============================================

## Group Members

* Stanley Huang <shuang1998@berkeley.edu>
* Kevin Liu <kevinliu36@berkeley.edu>
* Eric Li <eric.li5009@berkeley.edu>
* Ayush Maganahalli <ayush.sm@berkeley.edu>

# Task 1:  Argument Passing
## Data structures and functions
For this task, we are not going to make any changes to any existing structs nor are we going to create any new ones. We are only going to edit functions in `process.c`
## Algorithms
For this task, we mainly concern ourselves with the `start_process (void *file_name_)` function. Specifically, after when `load (const char *file_name, void (**eip) (void), void **esp)` is called and sets the esp to the top of the new process's stack in `setup_stack(esp)`, we will initiate `argc` and `argv[]` as indicated in 3.1.9 in the reference:
1. Split `const char* file_name` into words and set that array to be `argv[]` and set `argc` to be the length of that array.
2. Pass `argv[0]` into `load(...)` instead of the entire command
3. On successful load, push below `esp` the values  of each element in `argv` (word align if necessary). Then push pointers to each element (and a null pointer for `argv[argc]`), and then a final pointer that points to the pointer corresponding to the first element.
4. Push the value of `argc` and a dummy return address.

## Synchronization
No synchronization should be required for this task.
## Reasoning
Implementing this task is relatively straightforward and there were no real alternative solutions that we could find

# Task 2: Process Control Syscalls
## Data Structures and Functions
For task 2, we will be introducing a new structure, `struct wait_status`
```c
struct wait_status {
	int child_id;
    struct semaphore wait_semaphore;
    int exit_code;
    int child_counter;
    struct lock counter_lock;
    struct condition cond;
    list_elem elem;
}
```
This struct will help us keep track of the wait and execution statuses per parent-child relationship.

Furthermore, we will be adding fields to the current thread struct:
```c
struct thread {
	...
    struct list child_wait_statuses;
    struct wait_status* parent_wait_status;
    struct semaphore on_success;
    ...
}
```
Each `thread t`:
* will store every `struct wait_status` that correspond to `thread t` and its children in `struct list child_wait_statuses`
* will store the `wait_status` corresponding to `thread t` and its parent `parent_wait_status`
* will store an initially downed semaphore that will be upped when the process/thread is successfully loaded
By adding these fields each thread should have enough information to manage its own waiting and execution calls.

We also modify `syscall.c` to add if statements to handle the additional system calls in the spec, functions for `practice` and `halt`and modify the `exit`, `wait`, and `exec` implementations to match ours (see algorithms). We also need to add two functions for reading and writing to user memory to ensure access are in bounds
```c
static void syscall_handler (struct intr_frame)
int practice (int i)
void halt (void)
...
bool read_user_memory (void *address, void *buffer, size_t length)
bool write_user_memory (void *address, void *buffer, size_t length)
```
## Algorithms
__EXEC__: Our plan for handling SYS_EXEC simply involves initializing the appropriate wait statuses for both the child and the parent:
1. Call `process_execute` and retrieve the child process’ id.
2. Allocate with `malloc` and initialize a `struct wait_status* w` with the returned child_id. Initialize `wait_semaphore` as already down. Initialize `w->cond` and `w->lock` as well.
3. Append `struct wait_status* w` to `thread_current()->child_wait_statuses` and set the `parent_wait_status` of the child thread (with id equal to `w->child_id`) to `struct wait_status* w` as well
4. Acquire `thread_current()->parent_wait_status->counter_lock`, increment `thread_current()->parent_wait_status->child_counter`, and release the lock. This will make sure that `child_counter` accurately reflects the number of child processes the current process has.
5. Call `sema_down` on `child_thread->on_success`. This should block the current thread. Once `child_thread` has finished loading successfully, it will up its `on_success` semaphore, allowing its parent to return from exec.

__WAIT(PID)__: Our implementation for wait only requires three steps:
1. Find the thread, `child_thread` with the `tid_t` (or `pid_t`) matching the process id that is being passed in.
2. call `sema_down` on `child_thread->wait_semaphore`. This should block the current thread, causing the thread to wait. When `child_thread` is done executing and exits, it will up `child_thread->wait_semaphore`, causing the waiting parent thread to be unblocked.
3. return `child_thread->exit_code` (which will be updated when `child_thread` exits)

__EXIT__: Our exit syscall updates the appropriate `wait_status` struct. We let our parent process know that we have finished by decrementing the appropriate `child_counter` and by signaling the condition variable. We also make sure the current thread itself waits for each child thread to finish before freeing each `wait_status`:
1. Update `thread_current()->parent_wait_status->exit_code` with the appropriate exit code.
2. Acquire `thread_current()->parent_wait_status->lock` and decrement `thread_current()->parent_wait_status->child_counter`, keeping `child_counter` consistent with the number of running child processes.
3. Call `cond_signal` on `thread_current()->parent_wait_status->cond` and then release the lock. This signals the current thread's parent (which could be waiting) that the child_counter has been changed.
4. Then for each wait_status in `thread_current()->child_wait_statuses`:
	* Acquire `wait_status->lock`.
	* While `wait_status->counter != 0` call `cond_wait(wait_status->cond, wait_status->lock)`
	* Release the lock when the while loop terminates
	* This while loop ensures that all children finish before we free.
5. Free all `wait_status` in `thread_current()->child_wait_statuses`

__HALT__: Call `shutdown_power_off (void)`

__PRACTICE__: increment the `int i` argument and return it through the `eax` register

__ERROR HANDLING__: We will implement the second option suggested in the spec for accessing user memory
1. Check if address + length is below `PHYS_BASE`
2. Call functions provided in 3.1.7 (`get_user` and `put_user`)
3. Do this for all user memory accesses

For page faults we return -1 through the `eax` register, then update the `eip` register to the previous value in `eax`
## Synchronization

We use a number of synchronization primitives in our design:
* __Data Races__: The main concern we had for a data race involves the `child_counter` field of `struct wait_status`. Every time a child process is initialized we increment the counter and every time a child process exits we decrement the counter. Since multiple threads could potentially update/read the same counter we decided to use a simple lock to ensure all changes to the counter are atomic.
* __wait and exec atomicity__: To make sure that a waiting parent process continues executing after the specified child exits, we used `wait_semaphore` in `struct wait_status`. Since every parent-child relationship will have its own `wait_status`, if a parent waits, it can just attempt to down `wait_semaphore` which is initialized as already down. Once the child exits it will up the semaphore, thus ensuring that the child completes before the parent executes code after the wait call.
* __parent and child exit atomicity__: To make sure that the parent doesn't finish exiting until all its children have exited, we use a condition variable at `wait_status->cond`. The parent process trying to exit will `cond_wait` while the `wait_status->child_counter` is not 0. Every time this child_counter is updated by a child process, the condition variable will be signaled. Thus, a parent process will not finish its exit until the updated child_counter indicates that there are no more children running.
* __wait for child success__: To make sure a parent doesn't return from `exec` until a child has successfully loaded its executable, we use a semaphore `semaphore_on_success` initialized to 0. The parent will block on `sema_down` until the child calls `sema_up` after it successfully finishes calling `load`

## Rationale

Our major design decision was figuring out how to efficiently free our allocated `wait_status` structures. We decided to establish the invariant that a parent process will wait until all its child processes finish and then free the `wait_status` elements in the parent thread's `child_wait_statuses` list. We thought that allowing the parent to exit before the child would overly complicate freeing our `wait_status` structs, since a process could potentially have to free struct above itself and below itself.

# Task 3: File Operation Syscalls
## Data Structures and functions
We modify the thread struct in `thread.h`
```c
struct thread
{
    ...
    file ***fd_table;
    ...
}
```

We also modify `process.c` to create functions for our new system calls

```c
bool create (const char *file, unsigned initial_size)
bool remove (const char *file)
int open (const char *file)
int filesize (int fd)
int read (int fd, void *buffer, unsigned size)
int write (int fd, void *buffer, unsigned size)
void seek (int fd, unsigned position)
unsigned tell (int fd)
void close (int fd)
```
Finally we modify `syscall.c` by adding if statements to handle our new system calls and add a lock to ensure only one process can access the file system at a time
```c
struct lock filesys_lock;
```
## Algorithms
__Initializing a file descriptor table__: We use an array of file pointers to represent our file descriptor table
     1. Malloc a *file array of size 2 in `thread_create` for the `fd_table`
     2. Place `NULL` in `STDIN_FILENO` and `STDOUT_FILENO` of `fd_table`

__Resizing__: Each time we call `open`, we iterate through our `fd_table`. If we don't have room, we `realloc` to double the previous size

__Filesys__: For our `create` and `remove` functions, we simply call the corresponding function in `filesys.c`

__File__: For our `filesize`, `seek`, and `tell` functions, we simply call the corresponding function in `file.c`

__Opening and Closing a File__: To open a file, we call `filesys_open` to get a file pointer, then we iterate through `fd_table` until we find an open spot (represented by `NULL`), reallocating space if necessary. Once we find the first open spot we place the file pointer into `fd_table[i]` and use the index `i` as our file descriptor `fd`, which we return.

To close a file, we call `filesys_close`, providing `fd_table[fd]` as the argument. Then we clear that spot in `fd_table[fd]` by setting it to `NULL`.

__Reading and Writing__: For reading we check if `fd == 0`, which refers to `STDIN_FILENO`. If it does, we read from `stdin`. Else, we call `file_read` from `file.c`

For writing we check if `fd == 1`, which refers to `STDOUT_FILENO`. If it does, we write to `stdout`. Else, we call `file_write` from `file.c`.

## Synchronization
We need to prevent synchronization issues that may arise from multiple processes accessing the file system, so we use a lock `filesys_lock`. Each system call that involves the file system must first acquire the lock before doing anything, and release the lock when its done.

__ROX__: In order to avoid an executable from being modified while being used by another process in load, we acquire the lock at the beginning of load. This works because the other process that wants to modify the executable would also have to acquire the lock, meaning that if another process has already occupied the lock it should be impossible for the second process to modify it.

## Rationale
We chose to use an array as our file descriptor table because we can use array index as the corresponding `fd` for each file. This allows for easy access in constant time and a more elegant approach then say, a struct resembling a map from int to file pointers.

We malloc the file descriptor table to place it in the heap instead of the stack because placing it in the stack can cause issues when we have a large number of open files.

A single lock is sufficient to prevent synchronization issues in the file system. Although a more complicated solution would allow multiple processes to access the file system if they seek different files, the spec states that it is a issue for project 3 and thus, a later date.

## Additional Questions
1. The `sc-bad-sp` test file makes a syscall with an invalid stack pointer.
	-	Line 18: `asm volatile ("movl $.-(64*1024*1024), %esp; int $0x30");`
    -  `movl $.-(64*1024*1024), %esp`
        -  This is the instruction that moves an invalid address into the stack pointer
    -  `int $0x30`
    	-  This calls the syscall handler through an interrupt (0x30)
    - The program should exit with error code `-1` since the kernel will try to read the syscall number at the stack pointer, but the stack pointer is invalid.

2. The `sc-bad-arg` test file makes a syscall with a valid stack pointer, but with the arguments in unaccessible user memory.
	- Line 14 + 15: `movl $0xbffffffc, %%esp; movl %0, (%%esp); int $0x30 : : "i" (SYS_EXIT))`
	- `movl $0xbffffffc, %%esp`
		- Since `PHYS_BASE` is defined as `0xC0000000`, this instruction moves an address that is very close to the boundary of user and kernel memory into the stack pointer
   	- `movl %0, (%%esp)`
   		- Moves the value 0 into the location of the stack pointer
   	- `int $0x030 : : "i" (SYS_EXIT)`
   		- This places system call number (SYS_EXIT) at the top of the stack and then calls the syscall handler
   		- SYS_EXIT then looks at `args[1]` `(userprog/syscall.c:21)`, but this value is in Kernel memory which is not allowed and therefore exits with error code `-1`

3. Currently, the boundary tests are written such that it assumes the pages are all accessible by the user. However, we must also test the boundary case where we have data which begins in user space, but ends in kernel space. For example, `sc-bad-arg` sets the stack pointer just below `PHYS_BASE` to give enough room for the entire syscall number, but we need to test for the case if we don't give enough room for the entire  syscall number. We could simply write a test essentially similar to `sc-bad-arg` but instead have `movl $0xbffffffe, %%esp` which will put two bytes of the syscall number in user space, but the other two bytes in kernel space.

### GDB Questions
1. The name of the thread currently running is `main` and its address is `0xc000e000`. The only threads running are `main` and `idle_thread`.
```
pintos-debug: dumplist #0: 0xc000e000 {
	tid = 1,
    status = THREAD_RUNNING,
    name = "main", '\000' <repeats 11 times>,
    stack = 0xc000ee0c "\210",
    priority = 31,
    allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020},
    elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
    pagedir = 0x0,
    magic = 3446325067
 }

pintos-debug: dumplist #1: 0xc0104000 {
	tid = 2,
    status = THREAD_BLOCKED,
    name = "idle", '\000' <repeats 11 times>,
    stack = 0xc0104f34 "",
    priority = 0,
    allelem = {prev = 0xc000e020, next = 0xc0034b58 <all_list+8>},
    elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
    pagedir = 0x0,
    magic = 3446325067
}
```
2.
```
#0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/process.c:32
#1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288
	process_wait (process_execute (task));
#2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340
	a->function (argv);
#3  main () at ../../threads/init.c:133
	run_actions (argv);
```
3. The name of the thread currently running is `args-none` and its address is `0xc010a000`. The only threads running are `main`, `idle_thread`, and `args-none`.
```
0xc000e000 {
	tid = 1,
    status = THREAD_BLOCKED,
    name = "main", '\000' <repeats 11 times>,
    stack = 0xc000eebc "\001",
    priority = 31,
    allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020},
    elem = {prev = 0xc0036554 <temporary+4>, next = 0xc003655c <temporary+12>},
    pagedir = 0x0,
    magic = 3446325067
}

0xc0104000 {
	tid = 2,
	status = THREAD_BLOCKED,
    name = "idle", '\000' <repeats 11 times>,
    stack = 0xc0104f34 "",
    priority = 0,
    allelem = {prev = 0xc000e020, next = 0xc010a020},
    elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_list+8>},
    pagedir = 0x0,
    magic = 3446325067
}

0xc010a000 {
	tid = 3,
    status = THREAD_RUNNING,
    name = "args-none\000\000\000\000\000\000",
    stack = 0xc010afd4 "",
    priority = 31,
    allelem = {prev = 0xc0104020, next = 0xc0034b58 <all_list+8>},
    elem = {prev = 0xc0034b60 <ready_list>,
    next = 0xc0034b68 <ready_list+8>},
    pagedir = 0x0,
    magic = 3446325067
}
```

4. The thread running `start_process` is created at `process.c:45` in `process_execute`

```
Backtrace
#0  start_process (file_name_=0xc0109000) at ../../userprog/process.c:55
```
5. The page fault occurred at `0x0804870c`

6.
```
#0  _start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9
```
7. The line of code that causes the fault is `exit (main (argc, argv));`. This happens because it tries to access `argc` and `argv` but these haven't been put onto the stack yet which causes an invalid memory access.
