Design Document for Project 1: Threads
======================================

## Group Members

* Stanley Huang <shuang1998@berkeley.edu>
* Eric Li <eric.li5009@berkeley.edu>
* Ayush Maganahalli <ayush.sm@berkeley.edu>
* Kevin Liu <kevinliu36@berkeley.edu>

# Task 1: Efficient Alarm Clock
## Data Structures and Functions
**Sleeping Thread Queue/List:**
We will add a static list structure `sleeping_list` similar to `ready_list` in `thread.c`
that will hold all currently sleeping threads. With this list, our
scheduler will keep a pointer to each sleeping thread, making it possible to wake them up at
a later tick. List will be sorted by the new thread struct field `sleep_until` (see below),
for performance reasons.
```c
  static struct list sleeping_list;
```

**Thread Struct:**
We add a new field to the thread struct defined in thread.h to keep track of
the exact tick the thread will sleep until. Field set to -1 if not sleeping.
```c
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                          /* Thread identifier. */
  enum thread_status status;          /* Thread state. */
  char name[16];                      /* Name (for debugging purposes). */
  uint8_t *stack;                     /* Saved stack pointer. */
  int priority;                       /* Priority. */
  struct list_elem allelem;           /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem;              /* List element. */

  /* Added Field */
  int64_t sleep_until;               /* Tick thread will sleep until */
}

```
## Algorithms
**Setting thread to sleep**: This step's implementation will be in the new function, thread_sleep, which will be called in
`timer_sleep`:
```c
/* Put the current thread to sleep until wake_up_tick */
void thread_sleep(int64_t wake_up_tick);
```
This function takes in `wake_up_tick`, the value of the tick when the thread needs to be awake again.
Steps:
1. Set the current thread's `sleep_until` to `wake_up_tick`.
2. Acquire Lock for `sleeping_list`.
3. Insert the current into `sleeping_list`, such that `sleeping_list` remains sorted (ascending by `thread->sleep_until`)
4. Release lock and set thread status to BLOCKED

**Waking the thread**: Waking a sleeping thread simply requires the OS to check `sleeping_list` to see if there is a thread to be woken up.
Steps for every tick (in `thread_tick()`):
1. Check if `sleeping_list` is empty. If empty return, if not continue.
2. Check if first thread in `sleeping_list` has a `sleep_until` that is less than or equal the current tick. If false return, else continue.(Only need to check first element because `sleeping_list` is sorted, so first element will always need to be waken up first.)
3. Wake up current thread by setting status to READY. Then `thread_yield` to CPU.
4. Repeat steps 2 and 3 again for every thread who’s `sleep_until` is less than or equal to the current tick.

## Synchronization
**Shared Resource(s)** :
The only shared resource for this task is `sleeping_list`, the list containing all currently sleeping threads.

* Both the threads and the scheduler could access this resource. However, since the scheduler only reads data from `sleeping_list` to check when to wake up a certain thread, the only circumstance for a potential data race is when a thread gets interrupted in the middle of sleeping itself by a thread that also wants to sleep itself. Since we rely on the fact that `sleeping_list` should always be sorted, the possibility of threads being interrupted at arbitrary places in execution could cause that invariant to break.
* Thus, we make sure that when a thread wants to sleep, it acquires a lock on `sleeping_list` first, so that `sleeping_list` doesn't change in the middle of execution.

## Reasoning

**Alternatives**:
* We considered adding a new enum status, THREAD_SLEEPING, to denote that a thread is sleeping, and have the scheduler logic for sleeping revolve around the new enum. However, we realized this approach could be quite cumbersome code-wise, as we would have to ensure this new enum status is compatible with the rest of starter PintOS.
* For synchronization, we considered simply disabling all interrupts when a thread tries to sleep. This would prevent the potential data race for the `sleeping_list`, as well. However, we decided that using lock acquisition would be better. Disabling all interrupts would block *all* threads from interrupting, including those that won't even touch `sleeping_list`.
* For performance, we decided `sleeping_list` would be sorted as elements were added. Having this list sorted would allow the check to be O(1), as only the first element would need to be checked.

# Task 2: Efficient Alarm Clock
## Data Structures and Functions
**Donation Linked List Element**: We will be adding a new donation linked list element which will contain `lock` and `donated_priority`. This will be used to help us keep track of how much priority is donated because of a certain lock. We do this so that if a lock is released before the thread has finished executing, we can adjust the total effective priority accordingly because the other thread is no longer competing for a lock on the resource and therefore does not need to donate.
We also included a pointers to the threads involved in the donation to make it easier to track and remove donations after a lock is released.
```c
struct donation_list_elem
  {
    struct lock *lock;
   struct thread* donated_to_thread;
   struct thread* donated_from_thread;
   int donated_priority;
   struct list_elem elem;
  }
```

**Thread Struct**: We will be modifying the thread struct by adding an additional field which is a pointer to a list containing all the donations to the thread. The `donations` list should only include donations from threads which are waiting on it. We will also add a list of donations called `donated_to` which contain all the donations that this particular thread made (and is still currently waiting on). Finally, we added semaphores for both donation lists for synchronization purposes.
```c
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                           /* Thread identifier. */
  enum thread_status status;           /* Thread state. */
  char name[16];                       /* Name (for debugging purposes). */
  uint8_t *stack;                      /* Saved stack pointer. */
  int priority;                        /* Priority. */
  struct list_elem allelem;            /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem;               /* List element. */

  /* Added Field */
  int64_t sleep_until;                 /* Tick thread will sleep until */
  struct list donations;    	       /* List of all donations to this thread */
  struct list donated_to;              /* LIst of all donations from that this thread*/
  /* semaphores for synchronization */
  struct semaphore* donations_semaphore
  struct semaphore* donated_to_semaphore

}

```

## Algorithms
**Finding Effective Priority** :
* In our scheme, when a thread A donates its priority to thread B, A will add its *entire* effective priority to
to `threadB->donations`. Thus, if thread A has an effective priority of 100, the first element of `threadB->donations` will have
a `donated_priority` of 100.
* Thus, to find the effective priority of a thread, we first check `thread->donations` to see if its empty. If it is empty, no one has donated to this particular thread, so the effective priority is just `thread->priority`.
* If `thread->donations` is not empty, we find the donation with the *maximum* `donated_priority`, and use that value as the effective priority of the thread. We don't have to consider the thread's original priority, as the donated priorities are guaranteed to be greater than the original priority.

**Considering effective priority in semaphores (and condition variables)**:
To achieve this, we modify `sema_up()` to wake the thread with highest effective priority: `sema_up()` currently just pops the first thread in `waiters` and wakes it up. However, we want `sema_up()` to wake up the thread with the highest effective priority. In order to do this, `sema_up()` must return which thread has the highest effective priority and then continue execution from there. There are two ways to go about implementing this.
* In `sema_up()`, iterate through waiters and pop and unblock the thread with the largest effective priority.
 * This implementation will be easier to code as it allows us to still add waiters to the end in `sema_down()`
* In `sema_down()`, when adding threads to to `waiters` don't add them to the end, but instead insert them so that `waiters` remains sorted by each thread's effective priority.
 * This could prove to be more complicated as every time we add a waiter we must
   1. figure out where to insert it
   2. then actually insert it
The implementation for condition variables is quite similar since `condition` also has a list of `waiters`. We can either use the same methodology from `sema_up` for `cond_signal` or from `sema_down` for `cond_wait`.
(Synchronization note: for preventing data races for the waiter's list we will simply be disabling interrupts.)

**Aquiring Lock (Donations)**:
For our implementation of donations we modify `lock_acquire()` to donate when appropriate. There are two resulting scenarios when we call `lock_acquire()`, the resource being acquired is free to be acquired or it has already been acquired. This can be checked with `sema_try_down()`. For clarity, let `threadA` be our current thread trying to acquire the lock:
* Resource already acquired
 1. Figure out which thread currently holds the lock. (lock pointer passed as argument of `lock_acquire`, thus use `lock->holder`. Let this thread be `threadB`).
 2. If `threadA`'s effective priority is greater than `threadB`'s, create and initialize a `donation_list_elem` struct with a pointer to the lock, effective priority of the donating thread, and pointers to donating and recieving threads. If not, skip to 6
 3. Add this new `donation_list_elem` to `threadB->donations`.
 4. Update current thread's donated_to list, `threadA->donated_to`, by adding the `donation_list_elem` that was created in (2).
 * Steps 2-4 will most likely be implemented in a function called
   `donate(struct lock* lock, struct thread* from_thread, struct thread* to_thread)`. Thus, A donate to B would look like
   `donate(lock, threadA, threadB)`.
 5. At the end of `donate(lock, threadA, threadB)`, make it a recursive function by calling `donate(...)` on all the threads in `threadB->donated_to`: `donate(lock, threadB, ...)` (Make sure `from_thread` paramater changes to `threadB`)
 6. After `donate(...)` completes we sleep/block our current thread, `threadA`.
 7. Yield to scheduler.
* Resource free to acquire
 1. Acquire lock
 2. Continue with execution
(See Synchronization for synchronization method)

**Releasing Lock(Donations)**:
Our scheme makes it relativeley simple for releasing locks. If `threadA` wants to release its lock:
* Using the pointer to the lock, we can find each `donation_list_elem` in `threadA->donations` that contains the same lock.
* For each `donation_list_elem* d` that contains the same lock, get the thread this donation came from: `d->donated_from`. Then remove the corresponding donation element in `(d->donated_from)->donated_to`.
* Then remove all `donation_list_elem` that contains the lock from `threadA->donations`.
(See Synchronization for synchronization method)


**Priority Scheduling**:
Currently, our scheme relies on the fact that the current thread running will always have the highest effective priority out of all threads that are READY. To implement this we need `next_thread_to_run()` to consider the effective priority of each ready thread.
Similar to the waiting list for semaphores. there are two ways of implementing this:
1. Every time the scheduler looks for the next thread to run (`next_thread_to_run()`), instead of popping the first element, it will iterate through `ready_list` to find the highest priority thread.
2. Everytime a new thread becomes READY, we insert that thread to the `ready_list` so that `ready_list` is always sorted descending by effective priority (such that highest priority thread will always be first element).
In terms of performance, (2) is preferable. (See reasoning).



## Synchronization
**Ensure synchronization when modifying donation lists**: We can simply use a plain semaphore for each donation list in the thread struct (`donations` and `donated_to`) to ensure synchronization. References to these semaphores be within the thread struct as well. Thus, when a thread tries to modify a donation list it will attempt to down the corresponding semaphore. If another thread has already downed the same semaphore, it means that it is currently working on the same donation list. Thus, threads will access each donation_list atomically. Furthermore, since semaphore waiting lists now consider effective priority, higher priority threads will be allowed to modify donations before threads with lower priority.

**Ensure synchronization when modifying semaphore's waiters list**: Ensuring that the semaphore's waiters list gets properly synchronized proves to be a tougher issue as we are not allowed to use any given locks implementations. Thus, the only way to correctly/accurately modify this list is to disable all interrupts, execute the appropriate modifications, then re-enable all interrupts. In order to minimize the amount of code that is run when all interrupts are disabled, implementing the first option for `sema_up()` seems more intuitive. 

(Synchronization for condition variables seems to be already implemented with locks.)

## Reasoning
* In terms of our choice of data structures we decided to go with a single `donation_list_elem` struct containing all information about a donation. Although some processes might not need *every* single field within the struct, we decided for easy of implementation, code wise this was a more elegant approach. Having all donation processes use the same donation struct keeps the code consistent and readable.
* For our donation scheme, we decided that an entirely separate list of donations would be more manageable than actually editing the priority of the thread itself. This is because changing the original priority of thread is a much more risky implementation, as we could potentially edit the priority of a thread and lose the original value.
* For priority scheduling, we decided that method (2), having each waking thread inserted in a way such that `ready_list` remains sorted was a better approach. Though asymptotically both method (1) and (2) are both linear, it is much more preferable for `next_thread_to_run()` to be as fast as possible, as to not leave or OS without a thread to run. Thus, we sort teh `ready_list` as we add, so that `next_thread_to_run()` only needs to pop the first element to get the highest priority element.



# Task 3: Multi-level Feedback Queue Scheduler (MLFQS)
## Data Structures and Functions
**Thread Struct**: We need to modify the thread struct by adding 2 additional fields for `nice` and `recent_cpu`. The nice value will be an integer while the other will be represented as `fixed_point_t` values.
```c
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                           /* Thread identifier. */
  enum thread_status status;           /* Thread state. */
  char name[16];                       /* Name (for debugging purposes). */
  uint8_t *stack;                      /* Saved stack pointer. */
  int priority;                        /* Priority. */
  struct list_elem allelem;            /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem;               /* List element. */

  /* Added Field */
  int nice;                             /* Niceness value. */
  fixed_point_t recent_cpu              /* Recent cpu usage value. */
}
```
We will also be implementing the included functions `thread_get_nice`, `thread_set_nice` and `thread_get_recent_cpu` with the functionality specified in the spec.
```c
/* Returns the current thread's nice value */
int thread_get_nice(void)
```
```c
/* Sets the current thread's nice value to the new value input */
int thread_set_nice(int new_nice)
```
```c
/* Returns 100 times the thread's recent_cpu rounded to the nearest int */
int thread_get_recent_cpu(void)
```
We modify `thread_create` so new threads inherit the `nice` and `recent_cpu` values of the parent.
We also modify `thread_set_priority` to do nothing because a thread should not be updating its own priority.
**Static Variables**: We add a global `fixed_point_t` value `load_avg` to represent the systems load average calculated from number of ready threads and implement the included `thread_get_load_avg` function provided in each thread.
```c
/* System load average, estimates average number of threads in THREAD_READY state over the past minute */
static fixed_point_t load_avg;
```
```c
/* Returns 100 times the system load average, rounded to the nearest int */
int thread_get_load_avg(void)
```
**Multiple List Format**: Instead of using one list to store ready threads and sorting it based on priority, we will now use 64 separate lists; one for each priority level. This eliminates the need to sort threads because each list only contains threads of the same priority.
```c
/* List of processes in THREAD_READY state with priority 0 (PRI_MIN) */
static struct list ready_list_0;
/* List of processes in THREAD_READY state with priority 1 */
static struct list ready_list_1;
...
/* List of processes in THREAD_READY state with priority 63 (PRI_MAX) */
static struct list ready_list_63;
```
For ease of access, we will also add a global variable `advanced_ready_list` which will be an array of pointers to each thread list. This way we can easily access the list we want by syncing array index with list priority.
```c
/* Array of pointers to each of 64 thread lists; index of array refers to priority level of each list */
static struct list *advanced_ready_list[64];
```
**Priority Updating**: We add a global `ready_thread_count` to keep track of the number of threads that are ready to run or are currently running. This prevents us from having to iterate through thread_lists to figures out how many threads are in the ready state, and can easily be adjusted by incremented when threads are added to the queue and decremented when threads finish execution and are removed from the ready queue.
```c
/* Number of threads in the THREAD_READY or THREAD_RUNNING state; idle thread not counted */
static int ready_thread_count;
```
We modify `thread_tick` to increment the `recent_cpu` parameter of the current thread, and call 2 new update functions we create (`update_load_avg`, `update_iterator`) at the appropriate timing.
```c
/* Update load average using formula provided in spec */
void update_load_avg(int ready_thread_count)
````
```c
/* Iterate through all threads and call the function passed in as the argument */
void update_iterator(*func)
```
We create two update functions to pass into our iterator 
```c
/* Update recent cpu of thread using formula provided in spec */
void update_recent_cpu(thread* thread)
````
```c
/* Update priority of thread using formula provided in spec */
void update_priority(thread* thread)
````
## Algorithms
**Updating Recent Cpu and Priority**: The `recent_cpu` and `priority` of each individual thread need to be updated constantly. Our `advanced_ready_list` array is designed to simplify iterating through all the queues. Our update algorithm will consist of two passes. The first will update the `recent_cpu` and `priority` of each thread; the second will move threads to their new priority lists. Since threads generally decrease in priority, we iterate from lower to higher priority to avoid re-checking threads we've already moved.
* First pass
1. Iterate through `advanced_ready_list` (standard for loop from int i = 0 to i = 63)
2. If empty, move on to next priority (check `advanced_ready_list[i]`)
3. Iterate through list of threads
4. Update `recent_cpu` (call our update_cpu function)
5. Update `priority` (call our update_priority function)
* Second pass
1. Iterate through `advanced_ready_list`
2. If empty, move on to next priority
3. Iterate through list of threads
4. If priority of thread is different from array index (priority of list), delete thread from current list (update prev and next) and add thread to end of new priority list

**Scheduling Threads**: When choosing a new thread to run, the scheduler always chooses from the highest non-empty list. If there are multiple threads in the highest priority list, we use a round-robin system to ensure that all the threads in the list have similar access to the cpu.
* Scheduling
1. Iterate through `advanced_ready_list` (for loop from int i = 63(PRI_MAX) to int i = 0)
2. If empty, move on to next priority
3. If only one thread in list, run it
4. Run threads in same order as in thread list, giving each thread `TIME_SLICE` ticks before yielding
## Synchronization
Since `thread_tick` is where all the calculations happen, there won't be any synchronization issues since interrupts are disabled.
## Reasoning
* We are implementing the calculations given to us so there aren't many design choices to be made
* An array of pointers to our thread lists allows for constant access to the list we need

# Additional Questions
## Test
Assume we have 4 threads, A, B, C, and D. For the first 3, we can assign the priority values of 20, 30, and 40 (the actual values of B and C don’t matter too much, as long as B is less than C and both are less than D). Now, let’s say we also have a semaphore. Since C has the highest priority, we can have C take the semaphore and B will use the lock. Then, if we launch D (since B uses the lock) with priority 60, we can have B now use the semaphore. Currently we have the following thread priorities (in descending order): D (60), C (43), B (30), and A (20). Thus, D will now want the lock, leading A to reenable the semaphore, yield a thread, and reenable the semaphore again, since B and C both use the semaphore.

## Expected Output
Since B is going to receive a priority donation from D in order to resolve the lock, the first time the semaphore is enabled it should resolve B rather than C. Thus, the second call will inherently release C. As a result, if we were to print the threads in order of release from the semaphore, we print “B” and “C” in that order.

## Actual Output
As a result of the bug, though, we use the base priority instead of the effective priority. Thus, despite the priority donation from D to B, since B has a lower base priority than C (30 to 40), we end up releasing C first. That means the second call to release the semaphore is what releases B. Thus, if we printed the threads out, we get “C” and “B” in that order, the opposite of what we should have expected. Thus, thanks to the bug of using the base priority instead of the effective priority, we end up with a reserved order of release for the semaphore and incorrect output.

## Table

timer ticks | R(A) | R(B) | R(C) | P(A) | P(B) | P(C) | thread to run
------------|------|------|------|------|------|------|--------------
 0          |  0   |  0   |  0   |  63  |  61  |  59  |       A
 4          |  4   |  0   |  0   |  62  |  61  |  59  |       A
 8          |  8   |  0   |  0   |  61  |  61  |  59  |       B
12          |  8   |  4   |  0   |  61  |  60  |  59  |       A
16          |  12  |  4   |  0   |  60  |  60  |  59  |       B
20          |  12  |  8   |  0   |  60  |  59  |  59  |       A
24          |  16  |  8   |  0   |  59  |  59  |  59  |       C
28          |  16  |  8   |  4   |  59  |  59  |  58  |       B
32          |  16  |  12  |  4   |  59  |  58  |  58  |       A
36          |  20  |  12  |  4   |  58  |  58  |  58  |       C

## Ambiguities
There were some ambiguities in how to resolve which thread to run in certain scenarios, specifically when the priority of two threads were equal. For example, we can see that the first time P(A) and P(B) are equal is at tick 8. This was resolved by using the recency of a thread as the determinant in our queue. The least recently used was put to the front of the queue while the thread previous to that was loaded into the queue based on its priority in relation to the other entries in the queue. This is why A shows up before C in the aforementioned example, as P(A) = 61 and P(C) = 59, instead of the other way around if we only consider recency.

