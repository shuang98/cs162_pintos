# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek-past) begin
(seek-past) create "test.txt"
(seek-past) open "test.txt"
(seek-past) obtain filesize
(seek-past) seek past eof
(seek-past) end
seek-past: exit(0)
EOF
pass;
