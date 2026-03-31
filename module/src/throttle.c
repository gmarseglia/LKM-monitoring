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
	if (atomic_read(&st_cxt->throttle_running) == 0)
		return 0;

	unsigned long nr = syscall_get_nr(current, regs);
	struct kprobe **kprobe_context_p;

	/* Sanity check for preemption and interrupts */
	if (preempt_count() == 0) {
		pr_alert("%s: found preemptable, aborting.", __ST_MODNAME);
		return 0;
	}
	if (irqs_disabled()) {
		pr_alert("%s: irqs are disabled, aborting.", __ST_MODNAME);
		return 0;
	}

	/* If syscall request is critical */
	struct syscall_throttle_query_result st_qr;
	st_qr.nr = nr;
	if (unlikely(is_critical(&st_qr))) {
		/* curr_req is used as critical request ID */
		int curr_req = atomic_fetch_inc(&st_cxt->crit_req);

		__ST_LOG_FINE pr_info(
			"%s: probe #%05d hit, for pid %d, with ax=%lu",
			__ST_MODNAME, curr_req, current->pid, nr);

		/* If curr_avail < 0 ==> syscall has to be delayed */
		if (atomic_dec_return(&st_cxt->crit_avail) < 0) {
			ktime_t start;
			s64 delay_ms;

			start = ktime_get();

			/* If here, then syscall request has to be blocked */
			__ST_LOG_FINE pr_info(
				"%s: probe #%05d must be delayed, "
				"curr_req=%d",
				__ST_MODNAME, curr_req, curr_req);

			/* Write NULL in the kprobe context in the
			 * per-CPU memory */
			kprobe_context_p = this_cpu_ptr(
				(void *)st_cxt->saved_kprobe_ctx_offset);

			// this_cpu_write(kprobe_context_p, NULL);
			*kprobe_context_p = NULL;

			/* Enable preemption */
			atomic_inc(&st_cxt->crit_sleep);
			// #TODO: preempt_enable_no_resched() could be better
			preempt_enable();

			/* Go to sleep, and wake up when under limit or when
			 * throttling is off */
			wait_event_interruptible(
				st_cxt->critical_sleeping_wq,
				atomic_dec_return(&st_cxt->crit_avail) >= 0 ||
					atomic_read(
						&st_cxt->throttle_running) ==
						0);

			/* Disable premption */
			preempt_disable();
			atomic_dec(&st_cxt->crit_sleep);

			/* Restore kprobe context */
			// this_cpu_write(*kprobe_context_p, p);
			*kprobe_context_p = p;

			delay_ms = ktime_ms_delta(ktime_get(), start);

			struct syscall_throttle_delay_metrics *target_dm;
			target_dm = this_cpu_ptr(&st_dly_met);

			if (delay_ms > target_dm->max_delay_ms) {
				write_seqcount_begin(&target_dm->count);

				target_dm->max_delay_ms = delay_ms;

				memcpy(&target_dm->qr, &st_qr,
				       sizeof(struct
					      syscall_throttle_query_result));

				write_seqcount_end(&target_dm->count);
			}

			__ST_LOG_FINE pr_info(
				"%s: probe #%05d has completed with delay "
				"%lld ms",
				__ST_MODNAME, curr_req, delay_ms);
		}

		__ST_LOG pr_info("%s: probe #%05d completed, for pid %d",
				 __ST_MODNAME, curr_req, current->pid);
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
		pr_err("%s: register_kprobe failed, returned %d\n",
		       __ST_MODNAME, ret);
		return ret;
	}

	return 0;
}