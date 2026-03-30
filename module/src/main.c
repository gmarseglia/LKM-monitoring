#include "syscall-throttle.h"

DEFINE_PER_CPU(struct kprobe **, saved_kprobe_context_p);
static struct syscall_throttle_context __st_cxt;
struct syscall_throttle_context *st_cxt = &__st_cxt;

/*
  Module init function
*/
static int initfn(void)
{
	int ret;

	atomic_set(&st_cxt->hack_ready_on_cpu, 0);
	atomic_set(&st_cxt->throttle_running, 0);

	atomic_set(&st_cxt->crit_req, 0);
	atomic_set(&st_cxt->crit_sleep, 0);
	atomic_set(&st_cxt->crit_avail, __ST_BASE_CRIT_LIMIT);
	atomic_set(&st_cxt->crit_limit, __ST_BASE_CRIT_LIMIT);

	init_waitqueue_head(&st_cxt->critical_sleeping_wq);
	mutex_init(&st_cxt->ioctl_mutex);

	ret = load_hack_search();
	if (ret != 0)
		return ret;

	ret = load_metrics();
	if (ret != 0)
		return ret;

	ret = load_timer();
	if (ret != 0)
		return ret;

	ret = load_monitor();
	if (ret != 0)
		return ret;

	ret = load_throttle();
	if (ret != 0)
		return ret;

	ret = load_driver();
	if (ret != 0)
		return ret;

	ret = load_metrics_driver();
	if (ret != 0)
		return ret;

	pr_info("%s: module correctly loaded\n", __ST_MODNAME);

	return 0;
}

/*
  Module exit function
*/
static void exitfn(void)
{
	/*
	  Set atomic variable to general indication of running 0 indicates that
	  the module is starting tear down operations
	*/
	atomic_set(&st_cxt->throttle_running, 0);

	/* Unload the driver */
	unload_driver();
	unload_metrics_driver();

	/* Delete the timer */
	unload_timer();

	/*
	  Wake up possible sleeping thread.
	  running==0 makes all threads wake up
	*/
	update_limit_and_wake();

	/* Wait for all thread to exit the  */
	while (atomic_read(&st_cxt->crit_sleep) != 0)
		msleep(20);

	__ST_LOG_FINE pr_info("%s: all sleeping thread have completed\n",
			      __ST_MODNAME);

	/* Unregister the kprobe */
	unregister_kprobe(&st_cxt->probe_throttle);
	__ST_LOG_FINE pr_info("%s: kprobe at %p unregistered\n", __ST_MODNAME,
			      st_cxt->probe_throttle.addr);

	/* Unload the monitor */
	unload_monitor();

	/* Unload the metrics */
	unload_metrics();

	pr_info("%s: module correctly unloaded\n", __ST_MODNAME);
}

module_init(initfn);
module_exit(exitfn);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");
