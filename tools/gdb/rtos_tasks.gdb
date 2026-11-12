# Print every FreeRTOS task and where it is blocked (ARM_CM4F port).
# Needs a GDB server on :61234, e.g. ST-LINK_gdbserver -p 61234 -d -g -e.
# Freeze the IWDG first (DBGMCU_APB1_FZ |= 0x1800) or the radio resets while halted.
# Usage: arm-none-eabi-gdb -batch -x tools/gdb/rtos_tasks.gdb <elf>
set pagination off
set confirm off
target extended-remote localhost:61234

# Saved context on the task stack: r4-r11, EXC_RETURN, [s16-s31 if the task
# used the FPU], then the hardware frame r0-r3, r12, lr, pc, xpsr.
define _task
  set $tcb = (TCB_t *)$arg0
  set $stk = (unsigned int *)$tcb->pxTopOfStack
  set $exc = $stk[8]
  if (($exc & 0x10) == 0)
    set $hw = $stk + 9 + 16
  else
    set $hw = $stk + 9
  end
  printf "\n%s  prio %d\n", $tcb->pcTaskName, $tcb->uxPriority
  printf "  pc: "
  info line *$hw[6]
  printf "  lr: "
  info line *$hw[5]
end

define _list
  set $l = (List_t *)&$arg0
  set $end = (ListItem_t *)&$l->xListEnd
  set $it = (ListItem_t *)$l->xListEnd.pxNext
  while $it != $end
    set $owner = $it->pvOwner
    _task $owner
    set $it = (ListItem_t *)$it->pxNext
  end
end

printf "running now: %s\n", pxCurrentTCB->pcTaskName
set $p = 0
while $p < sizeof(pxReadyTasksLists)/sizeof(pxReadyTasksLists[0])
  printf "\n=== ready[%d]\n", $p
  _list pxReadyTasksLists[$p]
  set $p = $p + 1
end
printf "\n=== delayed\n"
_list xDelayedTaskList1
_list xDelayedTaskList2
printf "\n=== suspended\n"
_list xSuspendedTaskList
printf "\n=== pending ready\n"
_list xPendingReadyList
detach
quit
