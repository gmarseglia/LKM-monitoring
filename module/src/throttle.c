#include "linux/slab.h"
#include "syscall-throttle.h"

/*
  Perform the periodic update to handle limit:
    1. Updates the current limit
    2. Wakes up the the critical sleeping event wait queue
*/
void update_limit_and_wake(void)
{

	/* Updates the limits according the number of requests */
	int curr_lim = atomic_read(&sys_thr_cxt->crit_lim);
	int curr_req = atomic_read(&sys_thr_cxt->crit_req);
	int new_lim = min(curr_lim, curr_req) + CRITICAL_PER_UNIT;
	atomic_set(&sys_thr_cxt->crit_lim, new_lim);

	if (new_lim != curr_lim)
		LOG_FINE pr_info("%s: curr_lim=%d, curr_req=%d, new_lim=%d",
				 MODNAME, curr_lim, curr_req, new_lim);

	/* Wakes up the event wait queue */
	wake_up_interruptible(&sys_thr_cxt->critical_sleeping_wq);
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
	if (atomic_read(&sys_thr_cxt->running) == 0)
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
		int curr_req = atomic_fetch_inc(&sys_thr_cxt->crit_req);
		int curr_lim = atomic_read(&sys_thr_cxt->crit_lim);

		LOG_FINE pr_info("%s: probe #%05d hit, for pid %d, with ax=%lu",
				 MODNAME, curr_req, current->pid,
				 sys_call_number);

		/* If syscall request is critical and over limit */
		if (curr_req >= curr_lim) {
			LOG_FINE pr_info("%s: probe #%05d must be delayed, "
					 "curr_req=%d, curr_lim=%d",
					 MODNAME, curr_req, curr_req, curr_lim);

			/* Write NULL in the kprobe context in the per-CPU
			 * memory */
			struct kprobe **kprobe_context_p =
				this_cpu_read(saved_kprobe_context_p);
			this_cpu_write(*kprobe_context_p, NULL);

			/* Enable preemption */
			atomic_inc(&sys_thr_cxt->crit_sleep);
			// #TODO: preempt_enable_no_resched() could be better
			preempt_enable();

			/* Go to sleep until under limit or when removing the
			 * module */
			wait_event_interruptible(
				sys_thr_cxt->critical_sleeping_wq,
				curr_req < atomic_read(
						   &sys_thr_cxt->crit_lim) ||
					!atomic_read(&sys_thr_cxt->running));

			/* Disable premption */
			preempt_disable();
			atomic_dec(&sys_thr_cxt->crit_sleep);

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

	sys_thr_cxt->probe_throttle.symbol_name = DISPATCHER_SYMBOL_NAME;
	sys_thr_cxt->probe_throttle.pre_handler = pre_handler_throttle;

	ret = register_kprobe(&sys_thr_cxt->probe_throttle);
	if (ret < 0) {
		pr_err("%s: register_kprobe failed, returned %d\n", MODNAME,
		       ret);
		return ret;
	}

	return 0;
}