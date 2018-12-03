/* seeks past the end of a file, then tries to read and write */

#include <syscall.h>
#include "tests/userprog/sample.inc"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  int handle, byte_cnt, size;

  CHECK (create ("test.txt", sizeof sample - 1), "create \"test.txt\"");
  CHECK ((handle = open ("test.txt")) > 1, "open \"test.txt\"");

  CHECK (size = filesize (handle), "obtain filesize");
  if (size != sizeof sample - 1)
    fail ("filesize returned %d instead of %zu", size, sizeof sample - 1);

  seek (handle, size + 10);
  msg ("seek past eof");

  unsigned pos = tell (handle);
  if (pos != (unsigned)size + 10)
    fail ("failed to seek past eof");

  byte_cnt = read (handle, sample, sizeof sample - 1);
  if (byte_cnt != 0)
    fail ("read() failed to return 0");

  byte_cnt = write (handle, sample, sizeof sample - 1);
  if (byte_cnt != sizeof sample - 1)
    fail ("write() returned %d instead of %zu", byte_cnt, sizeof sample - 1);
}
