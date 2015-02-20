/*
 * Copyright (c) 2014-2015, Ari Suutari <ari@stonepile.fi>.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT,  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define NANOINTERNAL
#include <picoos.h>
#include <string.h>

#define SYSCALL_START_FIRST_CONTEXT    0
#define SYSCALL_SOFT_CONTEXT_SWITCH    1

int portIntNesting_g;
static inline void constructStackFrame(POSTASK_t task, void* stackPtr, POSTASKFUNC_t funcptr, void *funcarg);
void sysCall(int);

#if POSCFG_ENABLE_NANO != 0
#if NOSCFG_FEATURE_MEMALLOC == 1 && NOSCFG_MEM_MANAGER_TYPE == 1
void *__heap_start;
void *__heap_end;
#endif
#endif

unsigned char *portIrqStack;

extern unsigned int _end[];
extern unsigned int _splim[];

/*
 * Calculate stack bottom address based on _stack symbol generated by linker and
 * configured stack size.
 */
static inline void* __attribute__((always_inline)) stackBottom()
{
  return (void*) ((PORT_STACK_ALIGNMENT + (unsigned int) _splim) & ~(PORT_STACK_ALIGNMENT - 1));
}

#if POSCFG_ARGCHECK > 1
/*
 * Fill unused portion of IRQ stack with PORT_STACK_MAGIC.
 */
static inline void __attribute__((always_inline)) fillStackWithDebugPattern()
{
  register uint32_t si asm ("$sp");
  register unsigned char* s;

  si = si - 10; // Just to be sure not to overwrite anything
  s = (unsigned char*) si;
  while (s >= portIrqStack)
    *(s--) = PORT_STACK_MAGIC;

  *s = 0; // Separator between lowest stack location and heap
}

#endif

/*
 * Our own startup code (called by XC32 compiler startup code).
 * Provides region for heap (if using nano layer allocator) and
 * fills stack with debug pattern for overflow checking.
 */
void _on_bootstrap(void)
{
  /*
   * Start heap after .bss segment, align it upwards.
   * Reserve IRQ stack at top of memory, heap end before it.
   */
  portIrqStack = stackBottom();
  unsigned int a, b, siz;

   a = ((unsigned int) _stack);
   b = ((unsigned int) _splim);
   siz = a - b;
#if POSCFG_ENABLE_NANO != 0
#if NOSCFG_FEATURE_MEMALLOC == 1 && NOSCFG_MEM_MANAGER_TYPE == 1
  __heap_end = (void*) (portIrqStack - 4);
  __heap_start = (void*) (((unsigned int) _end + POSCFG_ALIGNMENT) & ~(POSCFG_ALIGNMENT - 1));
#endif
#endif

#if POSCFG_ARGCHECK > 1

  fillStackWithDebugPattern();

#if POSCFG_ENABLE_NANO != 0
#if NOSCFG_FEATURE_MEMALLOC == 1 && NOSCFG_MEM_MANAGER_TYPE == 1

  register unsigned char* s;

  s = (unsigned char*) __heap_start;
  while (s <= (unsigned char*) __heap_end)
  *(s++) = 'H';

#endif
#endif
#endif
}

/*
 * Initialize task stack frame. The layout must be same
 * as by context macros in arch_a_macros.h.
 */

static inline void constructStackFrame(POSTASK_t task, void* stackPtr, POSTASKFUNC_t funcptr, void *funcarg)
{
  unsigned int *stk, z;
  int r;
  struct PortStack* frame;

  /*
   * Get aligned stack pointer.
   */

  z = (unsigned int) stackPtr;
  z = z & ~(PORT_STACK_ALIGNMENT - 1);
  stk = (unsigned int *) z;

  /*
   * Put initial values to stack, including entry point address,
   * some detectable register values, status register (which
   * switches cpu to system mode during context switch) and
   * dummy place for exception stack pointer (see comments
   * assember files for this).
   */

  *(stk) = (unsigned int) 0x00000000; /* bottom           */
  *(--stk) = 0;                       /* 4 argument slots */
  *(--stk) = 0;
  *(--stk) = 0;
  *(--stk) = 0;

  frame = ((struct PortStack*) stk) - 1;

  for (r = 0; r < (sizeof (frame->r)) / sizeof (uint32_t) - 1; r++)
    frame->r[r] = r + 1;

  frame->mflo = 0;
  frame->mfhi = 0;
  frame->a0   = (uint32_t)funcarg;
  frame->ra   = (uint32_t)posTaskExit;
  frame->cp0Epc = (uint32_t)funcptr;
  frame->cp0Status = _CP0_STATUS_EXL_MASK | _CP0_STATUS_IE_MASK;

  task->stackptr = frame;
}

/*
 * Initialize task context.
 */

#if (POSCFG_TASKSTACKTYPE == 1)

VAR_t p_pos_initTask(POSTASK_t task, UINT_t stacksize, POSTASKFUNC_t funcptr, void *funcarg)
{

  unsigned int z;

  task->stack = NOS_MEM_ALLOC(stacksize);
  if (task->stack == NULL)
    return -1;

  task->stackSize = stacksize;

#if POSCFG_ARGCHECK > 1
  nosMemSet(task->stack, PORT_STACK_MAGIC, stacksize);
#endif

  z = (unsigned int) task->stack + stacksize - 2;
  constructStackFrame(task, (void*) z, funcptr, funcarg);
  return 0;
}

void p_pos_freeStack(POSTASK_t task)
{
  NOS_MEM_FREE(task->stack);
}

#elif (POSCFG_TASKSTACKTYPE == 2)

#if PORTCFG_FIXED_STACK_SIZE < 256
#error fixed stack size too small
#endif

VAR_t p_pos_initTask(POSTASK_t task,
    POSTASKFUNC_t funcptr,
    void *funcarg)
{
  unsigned int z;

#if POSCFG_ARGCHECK > 1
  memset(task->stack, PORT_STACK_MAGIC, PORTCFG_FIXED_STACK_SIZE);
#endif
  z = (unsigned int)task->stack + PORTCFG_FIXED_STACK_SIZE - 2;
  constructStackFrame(task, (void*)z, funcptr, funcarg);
  return 0;
}

void p_pos_freeStack(POSTASK_t task)
{
  (void)task;
}

#else
#error "Error in configuration for the port (poscfg.h): POSCFG_TASKSTACKTYPE must be 0, 1 or 2"
#endif

/*
 * Initialize CPU pins, clock and console.
 */

void p_pos_initArch(void)
{
  __builtin_disable_interrupts();
  
  // Enable multi-vector interrupt mode

  _CP0_BIS_CAUSE(1 << _CP0_CAUSE_IV_POSITION);
  INTCONbits.MVEC = 1;

  // Configure software interrupt 0 to lowest
  // IPL for delayed context switching.

  IFS0bits.CS0IF = 0;
  IPC0CLR = _IPC0_CS0IP_MASK | _IPC0_CS0IS_MASK;
  IPC0bits.CS0IP = 1;
  IPC0bits.CS0IS = 0;
  IEC0bits.CS0IE = 1;

  portInitClock();

#if NOSCFG_FEATURE_CONOUT == 1 || NOSCFG_FEATURE_CONIN == 1

  portInitConsole();

#endif
}

/*
 * Called by pico]OS to switch tasks when not serving interrupt.
 * Since we run tasks in system/user mode, "swi" instruction is
 * used to generate an exception to get into suitable mode
 * for context switching. 
 *
 * The actual switching is then performed by armSwiHandler.
 */

void p_pos_softContextSwitch(void)
{
  asm volatile("li $v0, %[sc]" : : [sc] "i" (SYSCALL_SOFT_CONTEXT_SWITCH));
  asm volatile("syscall");
}

/*
 * Called by pico]OS at end of interrupt handler to switch task.
 * Before switching from current to next task it uses
 * current task stack to restore exception mode stack pointer
 * (which was saved by saveContext macro).
 * After switching task pointers the new task's context is simply restored
 * to get it running.
 */

void PORT_NAKED p_pos_intContextSwitch(void)
{
  posCurrentTask_g = posNextTask_g;
  portRestoreContext();
}

/*
 * Called when context switch should be marked pending,
 * but cannot be done immediately.
 */

void p_pos_intContextSwitchPending(void)
{
  // Cause software interrupt, it will
  // be executed when all interrupts with
  // higher IPL are done.

  IFS0SET = _IFS0_CS0IF_MASK;
}

/*
 * Called by pico]OS to start first task. Task
 * must be prepared by p_pos_initTask before calling this.
 */
void PORT_NAKED p_pos_startFirstContext()
{
  // Set int nesting level to 1 as we are already on interrupt
  // stack here. It also disables saving context stack pointer
  // to task (which would be error, as there is not previous task here).

  portIntNesting_g = 1;
  asm volatile("li $v0, %[sc]" : : [sc] "i" (SYSCALL_START_FIRST_CONTEXT));
  asm volatile("syscall");
}

/*
 * Restore task context.
 */
void PORT_NAKED portRestoreContextImpl(void)
{
#if POSCFG_ARGCHECK > 1
  P_ASSERT("IStk", (portIrqStack[0] == PORT_STACK_MAGIC));
#endif

  // Setup up task stack pointer if we are
  // not returning from nested interrupt.

  asm volatile(".set push                    \n\t"
               ".set at                      \n\t"
               "addiu $t0, $sp, %[argslots]  \n\t"
               "lw    $t1, portIntNesting_g  \n\t"
               "add   $t1, $t1, -1           \n\t"
               "bne   $t1, $zero, 2f         \n\t"
               "lw    $t2, posCurrentTask_g  \n\t"
               "lw    $t0, %[stackptr]($t2)  \n"
    "2:\n\t"
    : : [argslots]"i" (PORT_STACK_ARGSLOTS), [stackptr]"i" (offsetof(struct POSTASK, stackptr)));

  // Restore most of the registers, using t0 as stack pointer

  PORT_GET_REG2(t0, t2, mfhi);
  PORT_GET_REG2(t0, t1, mflo);
  asm volatile("mthi   $t2");
  asm volatile("mtlo   $t1");
  PORT_GET_REG1(ra);
  PORT_GET_REG1(fp);
  PORT_GET_REG1(t9);
  PORT_GET_REG1(t8);
  PORT_GET_REG1(s7);
  PORT_GET_REG1(s6);
  PORT_GET_REG1(s5);
  PORT_GET_REG1(s4);
  PORT_GET_REG1(s3);
  PORT_GET_REG1(s2);
  PORT_GET_REG1(s1);
  PORT_GET_REG1(s0);
  PORT_GET_REG1(t7);
  PORT_GET_REG1(t6);
  PORT_GET_REG1(t5);
  PORT_GET_REG1(t4);
  PORT_GET_REG1(t3);
  PORT_GET_REG1(a3);
  PORT_GET_REG1(a2);
  PORT_GET_REG1(a1);
  PORT_GET_REG1(a0);
  PORT_GET_REG1(v1);
  PORT_GET_REG1(v0);

  // Disable interrupts, then restore remaining
  // registers.

  asm volatile ("di                          \n\t"
                "ehb                         \n\t"
                "lw    $t1, portIntNesting_g \n\t"
                "add   $t1, $t1, -1          \n\t"
                "sw    $t1, portIntNesting_g \n\t"
                 "move $sp, $t0              \n\t");

  PORT_GET_REG2(sp, t2, t2);
  PORT_GET_REG2(sp, t1, t1);
  PORT_GET_REG2(sp, t0, t0);
  asm volatile(".set noat");
  PORT_GET_REG2(sp, at, at);
  PORT_GET_REG2(sp, k0, cp0Epc);
  asm volatile("mtc0   $k0, $14");
  PORT_GET_REG2(sp, k0, cp0Status);
  asm volatile("addiu $sp, $sp, %[framesize] \n\t"
               "mtc0   $k0, $12              \n\t"
               "eret                         \n"
               ".set pop"
   : : [framesize] "i" (sizeof(struct PortStack)));
}

/*
 * Nothing to do, put CPU to sleep.
 */
void portIdleTaskHook()
{
  /*
   * Put CPU to idle or sleep.
   */
  asm volatile ("wait");
}

void PORT_NAKED _general_exception_context()
{
  portSaveContext1();
  portSaveContext3();

  register uint32_t cause asm("k0");
  register uint32_t callNum asm("v0");

  // Dig out exception cause. We are
  // really handling SYSCALL here.

  asm volatile ("mfc0 $k0, $13");
  cause = (cause >> 2) & 0x1f;
  if (cause != 8) {

   // Problems. Halt here.
    __builtin_disable_interrupts();
    while(1);
  }

  if (callNum != SYSCALL_START_FIRST_CONTEXT) {

    // When SYSCALL is executed the EPC
    // points to syscall instruction that
    // caused the exception. Adjust EPC to
    // point to next instruction so we continue
    // execution there.
    posCurrentTask_g->stackptr->cp0Epc += 4;
  }
  else
    portIntNesting_g--; // special case for first call.


  sysCall(callNum);
  portRestoreContext();
}

void sysCall(int callNum)
{
  switch (callNum)
  {
  case SYSCALL_SOFT_CONTEXT_SWITCH: // p_pos_softContextSwitch
    posCurrentTask_g = posNextTask_g;
    break;

  case SYSCALL_START_FIRST_CONTEXT: // Start first context
   break;

  default:
    __builtin_disable_interrupts();
    while(1);
    break;

  }
}

/*
 * Handle delayed task context switch.
 */
void PORT_NAKED __attribute__((vector(_CORE_SOFTWARE_0_VECTOR), nomips16)) Sw0Handler(void)
{
  portSaveContext();
  IFS0CLR = _IFS0_CS0IF_MASK;
  c_pos_intEnter();

  // Nothing else to do here. c_pos_intExit will
  // switch context to next task if required.

  c_pos_intExit();
  portRestoreContext();
}

#ifdef HAVE_PLATFORM_ASSERT
void p_pos_assert(const char* text, const char *file, int line)
{
// Something fatal, stay here forever.

  __builtin_disable_interrupts();
  while(1);
}
#endif
