# Clear every hardware breakpoint (FPB) and watchpoint (DWT) left armed by a
# GDB session that was killed instead of detached. They survive system resets
# and halt the core with nobody attached.
set pagination off
target extended-remote localhost:61234
delete
set *(unsigned int *)0xE0002008 = 0
set *(unsigned int *)0xE000200C = 0
set *(unsigned int *)0xE0002010 = 0
set *(unsigned int *)0xE0002014 = 0
set *(unsigned int *)0xE0002018 = 0
set *(unsigned int *)0xE000201C = 0
set *(unsigned int *)0xE0002020 = 0
set *(unsigned int *)0xE0002024 = 0
set *(unsigned int *)0xE0001028 = 0
set *(unsigned int *)0xE0001038 = 0
set *(unsigned int *)0xE0001048 = 0
set *(unsigned int *)0xE0001058 = 0
printf "FP_CTRL %08x FP_COMP0 %08x DWT_FUNCTION0 %08x\n", *(unsigned*)0xE0002000, *(unsigned*)0xE0002008, *(unsigned*)0xE0001028
detach
