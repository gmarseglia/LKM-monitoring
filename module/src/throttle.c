#include "asm/syscall.h"
#include "linux/compiler_attributes.h"
#include "linux/printk.h"
#include "syscall-throttle.h"

/*
  Perform the periodic update to handle limit:
    1. Updates the current limit
    2. Wakes up the the critical sleeping event wait queue
*/
void update_limit_and_wake(void)
{
	/* Updates the limits according the number of requests */
	atomic_set(&st_cxt->crit_avail, atomic_read(&st_cxt->crit_limit));

	/* Wakes up the event wait queue */
	wake_up_interruptible(&st_cxt->critical_sleeping_wq);
}

static unsigned long __st_dispatcher_addr;

static noinline void __st_throttle(void)
{
	msleep(1000);
	return;
}

static __attribute__((naked)) noinline void __st_dispatcher_pre_handler(void)
{
	asm volatile("push %rdi\n\t"
		     "push %rsi\n\t"
		     "push %rdx\n\t"
		     "push %rcx\n\t"
		     "push %rax\n\t"
		     "push %r8\n\t"
		     "push %r9\n\t"
		     "push %r10\n\t"
		     "push %r11\n\t"
		     "push %rbx\n\t"
		     "push %rbp\n\t"
		     "push %r12\n\t"
		     "push %r13\n\t"
		     "push %r14\n\t"
		     "push %r15\n\t");
	asm volatile("call *%0" : : "r"(__st_throttle));
	asm volatile("pop %r15\n\t"
		     "pop %r14\n\t"
		     "pop %r13\n\t"
		     "pop %r12\n\t"
		     "pop %rbp\n\t"
		     "pop %rbx\n\t"
		     "pop %r11\n\t"
		     "pop %r10\n\t"
		     "pop %r9\n\t"
		     "pop %r8\n\t"
		     "pop %rax\n\t"
		     "pop %rcx\n\t"
		     "pop %rdx\n\t"
		     "pop %rsi\n\t"
		     "pop %rdi\n\t");

	asm volatile("jmp *%0" : : "r"(__st_dispatcher_addr + 5));
}

/*
  Pre-handler for the dispatcher:
    1. Checks if the syscall request is critical
    2. If it's critical, checks the limit
    3. If it's critical and over limit, applies delay
*/
static int __kprobes pre_handler_throttle(struct kprobe *p,
					  struct pt_regs *regs)
{
	struct syscall_throttle_query_result st_qr;

	/* Check if the module is still running */
	if (!__ST_IS_ON)
		return 0;

	/* Sanity check for preemption and interrupts */
	if (preempt_count() == 0) {
		pr_alert("%s: found preemptable, aborting.\n", __ST_MODNAME);
		return 0;
	}
	if (irqs_disabled()) {
		pr_alert("%s: irqs are disabled, aborting.\n", __ST_MODNAME);
		return 0;
	}

	/* If syscall request is critical */
	st_qr.nr = syscall_get_nr(current, (struct pt_regs *)regs->di);

	if (unlikely(is_critical(&st_qr))) {
		regs->ip = (unsigned long)__st_dispatcher_pre_handler;
		return 1;
	}

	return 0;
}

/*
  Install the throttle pre-handler on the dispacther
*/
int load_throttle(void)
{
	int ret;

	st_cxt->probe_throttle.symbol_name = __ST_DISPATCHER_SYMBOL_NAME;
	st_cxt->probe_throttle.pre_handler = pre_handler_throttle;

	ret = register_kprobe(&st_cxt->probe_throttle);
	if (ret < 0) {
		pr_err("%s: register_kprobe on \"%s\" failed, returned %d\n",
		       __ST_MODNAME, __ST_DISPATCHER_SYMBOL_NAME, ret);
		return ret;
	}

	__st_dispatcher_addr = (unsigned long)st_cxt->probe_throttle.addr;

	return 0;
}

void unload_throttle(void)
{
	/* Wake up possible sleeping thread. running==0 makes all threads wake
	 * up
	 */
	clear_bit(__ST_FLAG_ON, __ST_FLAGS);
	wake_up_interruptible(&st_cxt->critical_sleeping_wq);

	/* Wait for all thread to exit the  */
	while (atomic_read(&st_cxt->crit_sleep) != 0)
		msleep(20);

	pr_info("%s: all sleeping thread have completed\n", __ST_MODNAME);

	/* Unregister the kprobe */
	unregister_kprobe(&st_cxt->probe_throttle);
}