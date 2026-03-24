#include "syscall-throttle.h"

DEFINE_PER_CPU(struct kprobe **, saved_kprobe_context_p);
struct syscall_throttle_context *sys_thr_cxt = NULL;

/*
  Module init function
*/
static int initfn(void)
{
	int ret;

	sys_thr_cxt =
		kmalloc(sizeof(struct syscall_throttle_context), GFP_KERNEL);

	atomic_set(&sys_thr_cxt->hack_ready_on_cpu, 0);
	atomic_set(&sys_thr_cxt->running, 1);
	atomic_set(&sys_thr_cxt->crit_req, 0);
	atomic_set(&sys_thr_cxt->crit_sleep, 0);
	atomic_set(&sys_thr_cxt->crit_avail, CRITICAL_PER_UNIT);
	init_waitqueue_head(&sys_thr_cxt->critical_sleeping_wq);
	mutex_init(&sys_thr_cxt->operation_synchronizer);

	pr_info("%s: module correctly loaded\n", MODNAME);

	ret = load_driver();
	if (ret != 0)
		return ret;

	ret = load_hack_search();
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
	atomic_set(&sys_thr_cxt->running, 0);

	/* Unload the driver */
	unload_driver();

	/* Delete the timer */
	del_timer_sync(&sys_thr_cxt->periodic_timer);
	pr_info("%s: timer unregistered\n", MODNAME);

	/*
	  Wake up possible sleeping thread.
	  running==0 makes all threads wake up
	*/
	update_limit_and_wake();

	/* Wait for all thread to exit the  */
	while (atomic_read(&sys_thr_cxt->crit_sleep) != 0)
		msleep(20);
	pr_info("%s: all sleeping thread have completed\n", MODNAME);

	/* Unregister the kprobe */
	unregister_kprobe(&sys_thr_cxt->probe_throttle);
	pr_info("%s: kprobe at %p unregistered\n", MODNAME,
		sys_thr_cxt->probe_throttle.addr);

	/* Unload the monitor */
	unload_monitor();

	kfree(sys_thr_cxt);
	pr_info("%s: module correctly unloaded\n", MODNAME);
}

module_init(initfn);
module_exit(exitfn);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");
