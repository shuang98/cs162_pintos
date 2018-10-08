Final Report for Project 1: Threads
===================================

Code Alterations
----------------

Since the initial design doc, the only changes made were optimizations suggested in the design doc review session with Jason:

  * __Part 1__: We switched our implementation to disable all interrupts rather than use locks for timer interrupts in order to avoid the timer interrupt case from being blocked, an edge case we didn't consider before the review.
  * __Part 2__: We made a few changes, the biggest being our donated_from and donated_to objects being single elements instead of lists, minimizing the space used to represent the information for priority donations. Similarly, we got rid of semaphores for syncs because when we acquire a lock we are already syncing, meaning that the act of using semaphores becomes redundant.
  * __Part 3__: Our implementation was mostly optimal by our considerations so no ideas were changed.

Group Reflection
----------------

The group worked together as a whole for most of the project. Most of the members did have specific sections they specialized in (i.e. worked on more and are more comfortable with explaining) but the group as a whole worked together to complete most parts. This entailed ideation, conceptualization, coding the initial code, debugging, and cleaning up the code. The specific sections covered by members were (i.e. most of the initial coding): Part 1 by Stanley Huang and Ayush Maganahalli, Part 2 by Stanley Huang, and Part 3 by Eric Li and Kevin Liu. We think this was a good way to handle the coding because it let the code be written by only a few members who knew exactly what their team was thinking but let the others also contribute via debugging and so that they could understand what was occurring as well.

Code Style
----------

In terms of safety, our code doesn't exhibit any unsafe methods. We included many ASSERT statements in order to ensure that the precondition before any potential calls followed the appropriate behavior. As a result, we could guarantee that the expected output should fall within an expected range of values/outputs. There weren't many memory issues we had to deal with as a whole but most of them.

The coding style was consistent across each file and blends in well with the Pintos code. It helps significantly that the code written only required a few lines, so it was easy to minimize how much code we had to write and stay close to the Pintos style. As a result, our code for many functions (e.g. the sleep/delay functions or the try vs do functions for semaphores and locks) ended up being similar and easy to work with for Pintos code. This allowed us to create reusable functions instead of duplicating code as well since we distilled the code to only what we needed, meaning we couldn't reuse code for each function.

Similar to the answer before, because our code is minimalistic and replicated relatively often, it is easy to understand. One can go through it and understand what each function does in relation to others, while also understanding the differences based on the change in numeric values or other attributes. The documentation (comments) is also helpful in explaining what the difference is for each edited function.

As a result, if our code did become more complicated we used comments to help document and explain the function of each function, without leaving commented-out code. Similarly, other etiquette like not committing binary files or using excessively long lines were followed as well.
