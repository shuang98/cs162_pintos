# Project 2 Final Report
## Code Alterations
### Task 1: Argument Passing
For this task we followed our design doc and the instructions from 3.1.9 (Program startup details) of the spec without any deviations. The main challenge of this part was making sure the correct values of argv were being mem-copied to the stack pointer.
### Task 2: Process Control Syscalls
**Data Structure Changes:**

We cut some fields from our wait status structure to simplify the process of freeing the wait status. Other than this we maintained all other data structures and functions from the initial design doc.
```c
struct wait_status {
  int child_id;
  int parent_id;
  struct semaphore wait_semaphore;
  struct semaphore load_semaphore;
  bool successfully_loaded;
  int exit_code;
  struct list_elem elem;
};
```
**Algorithm Changes:**
Our handling of SYS_EXEC, SYS_WAIT, and SYS_EXIT were the only sys calls that we changed. Instead of checking a counter field to tell whether or not a wait status is ready to free, we free a wait status if these conditions are met:
* The parent thread is about to exit
* The parent thread is done waiting for the child to finish

### Task 3: File Operation Syscalls
For this task we changed our file descriptor table implementation from an array to a linked list. This simplified code since we no longer had to resize our array with realloc when we ran out of slots or closed a bunch of files. We created a new `struct fd_elem` to hold the file pointer and the file descriptor number associated with it.
```c
struct fd_elem
	{
    	int fd;
        struct file* file_ptr;
        struct list_elem table_elem;
    }
```
Each thread has its own file descriptor table, so we added a pointer to the thread struct for the file descriptor list, and initialized the list in `thread_create`
```c
struct thread
	{
    	...
        struct list *fd_root;
        ...
    }
```
We also changed how we handled ensuring that executables were read only. In `load` we call `file_deny_write()` to make the file read only. Instead of closing the file after we finish loading (which undos the deny write), we instead close the file in `thread_exit`. In `thread_exit` we close every file in the file descriptor list, and free all the memory we malloc'd for the nodes.

For the actual syscalls we followed our original design except the syscalls that required us to maintain the file descriptor array now involved us maintaining the linked list.
1. Open: We iterate through the list to find the first empty fd, and create a new node with that fd and our newly opened file. Then we add the `fd_elem` to the list using `list_insert_ordered()`.
2. Close: We iterate through the list until we find the node with the corresponding fd, and we remove that node, freeing any memory we need to.
3. Read: We iterate through the list to find the file pointer associated with the given fd
4. Write: Same as read

We kept the linked list ordered by fd to simplify checking whether a fd number was open or not. We used a `int count` that started at 2 and iterated through the list. Whenever the count didn't match the fd of the current node, we knew that there was no file with that fd (since the list is ordered).
## Group Reflection
### Distribution of work
* Eric Li: Task 1, Task 3, Design Doc Task 3, User test 2, Debugging (t1, t3)
* Stanley Huang: Task 2, Design Doc Task 1, Design Doc Task 2, Debugging (t2)
* Kevin Liu: Task 1, Task 3, Design Doc Additional Questions, GDB Questions, User test 1, Debugging (all)
* Ayush Maganahalli: Task 2, Design Doc Task 2, Debugging (t2)
### What went well
Our initial design doc was well thought out, so we didn't need to change much conceptually. The main implementation changes made were based on points our design TA pointed out in the design review. Debugging in general was much simpler and quicker since there wasn't any scheduling involved which can be non-deterministic and difficult to identify the source of the bugs. Our team dynamics and distribution of work was better this time (compared to project 1) as well.
### What could be improved
We ended up using a slip day and gave up on one test case (multi-oom which was only 1 point). This mainly resulted from us not starting early enough which will hopefully not be a problem during project 3 since midterms are pretty much over.
## Student Testing Report
### Test 1: sc-bad-bndry
1. For this test we test our syscall handler to see if it handles the case where the stack pointer is pointing to valid user memory, but the arguments to the syscall are not.
2. First we move the stack pointer to `0xbffffffc` which is right below `PHYS_BASE`. Then we put the syscall number for `EXIT` into that area of memory. In the syscall handler we should page fault when checking `args[1]` for the argument for `EXIT` since we are trying to access kernal memory, and then exit with code `-1`.
3. Output:
```
Copying tests/userprog/sc-bad-bndry to scratch partition...
qemu -hda /tmp/EuMLEXTfX3.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run sc-bad-bndry
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 175 sectors (87 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 101 sectors (50 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'sc-bad-bndry' into the file system...
Erasing ustar archive...
Executing 'sc-bad-bndry':
(sc-bad-bndry) begin
Page fault at 0xc0000001: rights violation error writing page in user context.
sc-bad-bndry: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x80480a8
 cr2=c0000001 error=00000007
 eax=00000100 ebx=00000000 ecx=0000000e edx=00000027
 esi=00000000 edi=00000000 esp=bffffffe ebp=bfffffba
 cs=001b ds=0023 es=0023 ss=0023
sc-bad-bndry: exit(-1)
Execution of 'sc-bad-bndry' complete.
Timer: 64 ticks
Thread: 6 idle ticks, 55 kernel ticks, 3 user ticks
hda2 (filesys): 61 reads, 206 writes
hda3 (scratch): 100 reads, 2 writes
Console: 1264 characters output
Keyboard: 0 keys pressed
Exception: 1 page faults
Powering off...
```
4. Result:
```
PASS
```
5. If the kernel didn't page fault on the syscall argument memory access because it didn't properly check the boundaries, the test wouldn't page fault and call exit with a random garbage value (depending on what exists at the bottom of kernel memory). This is an issue because kernel memory shouldn't be accessed by a user program, and the exit code provides valuable information and a non-deterministic value can cause confusion or misdirection.
6. If the kernel failed to `exit` or provide the proper exit code (`-1`), then our test would have failed. Not exiting the thread may cause the system to hang since pintos can only run one thread at a time and the page fault means the thread can't be properly execute the rest of its code. An incorrect exit code is an issue because it will not communicate the fact that a page fault occured but instead suggest the thread exited for another reason, causing confusion and misdirection (if say the exit code was 1, the thread exited normally).
### Test 2: seek-past
1. This test tests several of the file system syscalls that aren't really tested by the provided test cases. Notably `filesize()`, `seek()`, `tell()`. The checks for filesize and tell are mostly sanity checks; the main functionality this test checks for is the case where we seek past the end of the file.
2. In the test we create a file called `test.txt` with the size of `sample.txt - 1`, then open it. We then check `filesize` to see if the created file has the size we specified in `create`. Then we `seek` 10 bytes past the end of the file, and check that `tell` returns the proper offset (`sizeof (sample.txt + 9)` which is the size of the file + 10). Finally we try to `read` and `write` to the file, which should both return 0 bytes and not crash. Write currently should return zero because files in our filesystem are currently of fixed size.
3. Output:
```
Copying tests/userprog/seek-past to scratch partition...
Copying ../../tests/userprog/sample.txt to scratch partition...
qemu -hda /tmp/S77dtIM8qI.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading..........
Kernel command line: -q -f extract run seek-past
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 175 sectors (87 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 106 sectors (53 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'seek-past' into the file system...
Putting 'sample.txt' into the file system...
Erasing ustar archive...
Executing 'seek-past':
(seek-past) begin
(seek-past) create "test.txt"
(seek-past) open "test.txt"
(seek-past) obtain filesize
(seek-past) seek past eof
(seek-past) end
seek-past: exit(0)
Execution of 'seek-past' complete.
Timer: 64 ticks
Thread: 6 idle ticks, 56 kernel ticks, 2 user ticks
hda2 (filesys): 115 reads, 223 writes
hda3 (scratch): 105 reads, 2 writes
Console: 1043 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```
4. Result:
```
PASS
```
5. If the file system and file descriptor table aren't implemented correctly (i.e. opening the wrong file or returning the incorrect file descriptor), after we open the file we create, our test may fail at the filesize check since we are accessing the wrong file or in the worse case call `seek` on another equal size file, which will pass our test (despite buggy implementation) but may cause bugs if the other file is opened by a different thread that is unaware the file position has changed.
6. If the file system isn't implemented correctly (i.e. allowing writes to a file even if out of bounds without changing the size of the file) our test will access memory it isn't supposed and essentially will buffer overflow where it writes information past the end of the file which may result in a seg fault or cause bugs by changing data in memory it is not supposed to.
### Experience
The provided test cases for userprog were in general pretty solid, testing for null or bad inputs, as well as basic and more complex functionality. Synchronization is a very important aspect of operating systems, so there could have been more test cases that involved many threads running complex tasks to help find obscure bugs. The filesystem test cases also only really tested the ones that interacted with our file descriptor table implementation which were `open`, `close`, `read`, and `write`. The other syscalls were mostly neglected (`filesize`, `seek`, `tell`).

Writing test cases helped us learn because it forces us to understand exactly what our program is supposed to do, as well as think of edge cases and how those should be handled. We also got introduced to how tests for pintos are written and got to work with the perl language a tiny bit which was cool.
