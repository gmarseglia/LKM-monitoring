#include "syscall-throttle.h"

struct syscall_throttle_sleep_metrics *st_slp_met = NULL;

void update_sleep_metrics(void)
{
	if (atomic_read(&st_cxt->throttle_running) == false)
		return;

	spin_lock_bh(&st_slp_met->lock); // #TODO: investigate more on _bh

	int curr_sleep = atomic_read(&st_cxt->crit_sleep) *
			 SYS_THR_METRICS_SCALING_FACTOR;

	st_slp_met->max_sleep = MAX(curr_sleep, st_slp_met->max_sleep);

	unsigned long total_sleep =
		st_slp_met->avg_sleep * st_slp_met->units_passed;

	st_slp_met->units_passed++;
	st_slp_met->avg_sleep =
		(total_sleep + curr_sleep) / st_slp_met->units_passed;

	pr_info("%s: max_sleep=%lu, avg_sleep=%lu, units_passed=%lu\n", MODNAME,
		st_slp_met->max_sleep / SYS_THR_METRICS_SCALING_FACTOR,
		st_slp_met->avg_sleep / SYS_THR_METRICS_SCALING_FACTOR,
		st_slp_met->units_passed);

	spin_unlock_bh(&st_slp_met->lock);
}

int load_metrics(void)
{
	/* Allocate the struct */
	st_slp_met =
		kzalloc(sizeof(struct syscall_throttle_sleep_metrics), GFP_KERNEL);
	if (!st_slp_met) {
		pr_err("%s: failed to allocate memory for "
		       "syscall_throttle_metrics",
		       MODNAME);
		return -ENOMEM;
	}

	/* Initialize the lock */
	spin_lock_init(&st_slp_met->lock);

	return 0;
}

void unload_metrics(void) { kfree(&st_slp_met); }