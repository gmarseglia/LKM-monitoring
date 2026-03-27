#include "syscall-throttle.h"

struct syscall_throttle_metrics *sys_thr_met = NULL;

static inline void update_metrics(void)
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

/*
  The timer callback executed periodically:
    1. Calls the update_limit_and_wake routine
    2. Re-arms the timer
*/
static void timer_callback(struct timer_list *t)
{
	update_metrics();

	update_limit_and_wake();

	/* Re-arm the timer to fire again in TIMER_INTERVAL milliseconds */
	mod_timer(&sys_thr_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(TIMER_INTERVAL));
}

/*
  Load the periodic timer
*/
int load_timer(void)
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

	/* Setup first execution of the timer routine */
	timer_setup(&sys_thr_cxt->periodic_timer, timer_callback, 0);
	mod_timer(&sys_thr_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(TIMER_INTERVAL));

	return 0;
}

/* Un load the timer */
void unload_timer(void)
{
	timer_delete_sync(&sys_thr_cxt->periodic_timer);

	kfree(&sys_thr_met);

	pr_info("%s: timer unregistered\n", MODNAME);
}