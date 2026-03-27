#include "syscall-throttle.h"

struct syscall_throttle_metrics *sys_thr_met = NULL;

void update_sleep_metrics(void)
{
	if (atomic_read(&sys_thr_cxt->throttle_running) == false)
		return;

	spin_lock_bh(&sys_thr_met->lock); // #TODO: investigate more on _bh

	int curr_sleep = atomic_read(&sys_thr_cxt->crit_sleep) *
			 SYS_THR_METRICS_SCALING_FACTOR;

	sys_thr_met->max_sleep = MAX(curr_sleep, sys_thr_met->max_sleep);

	unsigned long total_sleep =
		sys_thr_met->avg_sleep * sys_thr_met->units_passed;

	sys_thr_met->units_passed++;
	sys_thr_met->avg_sleep =
		(total_sleep + curr_sleep) / sys_thr_met->units_passed;

	pr_info("%s: max_sleep=%lu, avg_sleep=%lu, units_passed=%lu\n", MODNAME,
		sys_thr_met->max_sleep / SYS_THR_METRICS_SCALING_FACTOR,
		sys_thr_met->avg_sleep / SYS_THR_METRICS_SCALING_FACTOR,
		sys_thr_met->units_passed);

	spin_unlock_bh(&sys_thr_met->lock);
}

int load_metrics(void)
{
	/* Allocate the struct */
	sys_thr_met =
		kzalloc(sizeof(struct syscall_throttle_metrics), GFP_KERNEL);
	if (!sys_thr_met) {
		pr_err("%s: failed to allocate memory for "
		       "syscall_throttle_metrics",
		       MODNAME);
		return -ENOMEM;
	}

	/* Initialize the lock */
	spin_lock_init(&sys_thr_met->lock);

	return 0;
}

void unload_metrics(void) { kfree(&sys_thr_met); }