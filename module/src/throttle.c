#include "linux/stddef.h"
#include "syscall-throttle.h"

/*
  Perform the periodic update to handle limit:
    1. Updates the current limit
    2. Wakes up the the critical sleeping event wait queue
*/
void update_limit_and_wake(void)
{
	/* Updates the limits according the number of requests */
	atomic_set(&st_cxt->crit_avail, CRITICAL_PER_UNIT);

	/* Wakes up the event wait queue */
	wake_up_interruptible(&st_cxt->critical_sleeping_wq);
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

	/* Check if the module is still running */
	if (atomic_read(&st_cxt->throttle_running) == false)
		return 0;

	struct pt_regs *the_regs = (struct pt_regs *)regs->di;
	unsigned long sys_call_number =
		the_regs->orig_ax; // #TODO: why orig_ax works?

	/* Sanity check for preemption and interrupts */
	if (preempt_count() == 0) {
		pr_alert("%s: found preemptable, aborting.", MODNAME);
		return 0;
	}
	if (irqs_disabled()) {
		pr_alert("%s: irqs are disabled, aborting.", MODNAME);
		return 0;
	}

	/* If syscall request is critical */
	if (unlikely(is_critical(sys_call_number))) {
		/* curr_req is used as critical request ID */
		int curr_req = atomic_fetch_inc(&st_cxt->crit_req);

		LOG_FINE pr_info("%s: probe #%05d hit, for pid %d, with ax=%lu",
				 MODNAME, curr_req, current->pid,
				 sys_call_number);

		/* If curr_avail < 0 ==> syscall has to be delayed */
		if (atomic_dec_return(&st_cxt->crit_avail) < 0) {
			/* If here, then syscall request has to be blocked */
			LOG_FINE pr_info("%s: probe #%05d must be delayed, "
					 "curr_req=%d",
					 MODNAME, curr_req, curr_req);

			/* Write NULL in the kprobe context in the
			 * per-CPU memory */
			struct kprobe **kprobe_context_p =
				this_cpu_read(saved_kprobe_context_p);
			this_cpu_write(*kprobe_context_p, NULL);

			/* Enable preemption */
			atomic_inc(&st_cxt->crit_sleep);
			// #TODO: preempt_enable_no_resched() could be better
			preempt_enable();

			/* Go to sleep, and wake up when under limit or when
			 * throttling is off */
			wait_event_interruptible(
				st_cxt->critical_sleeping_wq,
				atomic_dec_return(&st_cxt->crit_avail) >=
						0 ||
					atomic_read(
						&st_cxt
							 ->throttle_running) ==
						false);

			/* Disable premption */
			preempt_disable();
			atomic_dec(&st_cxt->crit_sleep);

			/* Restore kprobe context */
			this_cpu_write(*kprobe_context_p, p);

			LOG_FINE pr_info("%s: probe #%05d has completed delay",
					 MODNAME, curr_req);
		}

		LOG pr_info("%s: probe #%05d completed, for pid %d", MODNAME,
			    curr_req, current->pid);
	}

	return 0;
}

/*
  Install the throttle pre-handler on the dispacther
*/
int load_throttle(void)
{
	int ret;

	st_cxt->probe_throttle.symbol_name = DISPATCHER_SYMBOL_NAME;
	st_cxt->probe_throttle.pre_handler = pre_handler_throttle;

	ret = register_kprobe(&st_cxt->probe_throttle);
	if (ret < 0) {
		pr_err("%s: register_kprobe failed, returned %d\n", MODNAME,
		       ret);
		return ret;
	}

	return 0;
}