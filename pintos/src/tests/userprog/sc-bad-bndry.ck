# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(sc-bad-bndry) begin
Page fault at 0xc0000001: rights violation error writing page in user context.
sc-bad-bndry: dying due to interrupt 0x0e (#PF Page-Fault Exception).
Interrupt 0x0e (#PF Page-Fault Exception) at eip=0x80480a8
 cr2=c0000001 error=00000007
 eax=00000100 ebx=00000000 ecx=0000000e edx=00000027
 esi=00000000 edi=00000000 esp=bffffffe ebp=bfffffba
 cs=001b ds=0023 es=0023 ss=0023
sc-bad-bndry: exit(-1)
EOF
pass;
