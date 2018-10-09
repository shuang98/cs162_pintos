Final Report for Project 1: Threads
===================================

Code Alterations
----------------

Since the initial design doc, the only changes made were optimizations suggested in the design doc review session with Jason:

  __Part 1__: We switched our implementation to disable all interrupts rather than use locks for timer interrupts in order to avoid the timer interrupt case from being blocked, an edge case we didn't consider before the review. 

  __Part 2__: We made a few changes, the biggest being our `donated_to` field in the `thread` struct was changed to a single `donation` struct. Previously, `donated_to` had been a list of `donation` structs; however, we learned in our design review that only keeping track of a single `donation` is necessary. 
  Our changes as follows 
  (certain fields have been renamed for clarity `donated_to` -> `current_donation` and `donations` -> `received_donations`):
  ```c
  struct thread
    {
      //...
      struct list received_donations;    	       /* List of all donations to this thread */
      struct donation current_donation;              /* Current donation that came from this thread */
      //...
    }
  ```
  With this change of `struct thread`, we made the appriopriate changes to our donation algorithm as well. Specifically, having `struct thread* A` donate to `struct thread* B` is simplified where only `A->current_donation` gets updated, and a pointer to `A->current_donation` gets added to `B->received_donations` if not already there.

  __Part 3__: For part 3, we implemented a more improved algorithm for updating thread priorites and locations within the 64 queues. Previously, we decided on using two seperate passes on the queue one for updating priority and the next for moving threads if necessary. However, found a way to do it in one pass:
  * Since calling `void thread_set_nice (int nice)` triggers the current thread to update its priority and place in queue on its own our update to `thread_currnt()->priority` every 4 ticks will always be decreasing the priority (as the only time a thread's priority increases is when it sets its own niceness). 
  * Thus, anytime a thread needs to be moved to a different priority level queue, it will always be moving down.
  * Therefore, if we iterate through the queue _from bottom to top (0->64)_, we can update priority and and then placement for each thread without any recalculations; i.e we will never encounter the same thread while iterating.

  Other than this change, our methods remained the same.

Group Reflection
----------------
Work Distribution
  * Eric Li: Part1, Part3, Design Doc Pt3
  * Stanley Huang: Part1, Part2, Part3, Design Doc Pt1 $ 2
  * Kevin Liu: Part1, Part3, Design Doc Pt2
  * Ayush Maganahalli: Design Doc Ques.

**What went well**: Our initial design document was pretty well thought out and required only a few changes, so implementation was relatively straightforward. Most of coding time was spent debugging.

**What could be improved**: We could have improved certain aspects of collaboration, like distribution of work. We also encoutered some git problems that set us back a few times.
