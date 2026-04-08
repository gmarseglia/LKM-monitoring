#include "syscall-throttle.h"

/*
  Perform the periodic update to handle limit:
    1. Updates the current limit
    2. Wakes up the the critical sleeping event wait queue
*/
void update_limit_and_wake(void)
{
	/* Updates the available token, as the current limit */
	atomic_set(&st_cxt->crit_avail, atomic_read(&st_cxt->crit_limit));

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
	int curr_req;
	int ret = 0;
	ktime_t start;
	s64 delay_ms = 0;
	struct syscall_throttle_query_result st_qr;
	struct syscall_throttle_delay_metrics *target_dm;

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
		/* curr_req is used as critical request ID */
		curr_req = atomic_fetch_inc(&st_cxt->crit_req);

		pr_debug("%s: probe #%05d hit, with ax=%d\n", __ST_MODNAME,
			 curr_req, st_qr.nr);

		/* If curr_avail < 0 ==> syscall has to be delayed */
		if (atomic_dec_return(&st_cxt->crit_avail) < 0) {
			start = ktime_get();
			atomic_inc(&st_cxt->crit_sleep);

			/* If here, then syscall request has to be blocked */
			pr_debug("%s: probe #%05d must be delayed\n",
				 __ST_MODNAME, curr_req);

			/* Write NULL in the kprobe context in the
			 * per-CPU memory */
			this_cpu_write(*st_cxt->saved_kprobe_ctx_offset, NULL);

			/* Enable preemption */
			preempt_enable();

			/* Go to sleep, and wake up when under limit or when
			 * throttling is off */
			ret = wait_event_interruptible(
				st_cxt->critical_sleeping_wq,
				atomic_dec_return(&st_cxt->crit_avail) >= 0 ||
					!__ST_IS_ON);

			printk("%s: wait_event_interruptible returned %d.\n",
			       __ST_MODNAME, ret);

			/* Disable premption */
			preempt_disable();

			/* Restore kprobe context */
			this_cpu_write(*st_cxt->saved_kprobe_ctx_offset, p);

			atomic_dec(&st_cxt->crit_sleep);

			/* Update per-CPU metrics */
			delay_ms = ktime_ms_delta(ktime_get(), start);
			target_dm = this_cpu_ptr(&st_dly_met);
			if (delay_ms > target_dm->max_delay_ms) {
				write_seqcount_begin(&target_dm->count);

				target_dm->max_delay_ms = delay_ms;
				memcpy(&target_dm->qr, &st_qr,
				       sizeof(struct
					      syscall_throttle_query_result));

				write_seqcount_end(&target_dm->count);
			}
		}

		pr_debug("%s: probe #%05d completed, with delay %lld ms\n",
			 __ST_MODNAME, curr_req, delay_ms);
	}

	if (ret == 0) {
		return 0;
	} else {
		unsigned long ret_addr = *(unsigned long *)regs->sp;
		regs->ip = ret_addr;
		regs->sp += sizeof(long);
		regs->ax = -EPERM;
		return 1;
	}
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