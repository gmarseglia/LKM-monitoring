#include "syscall-throttle.h"

static struct syscall_throttle_context __st_cxt;
struct syscall_throttle_context *st_cxt = &__st_cxt;

/*
  Module init function
*/
static int initfn(void)
{
	int ret;

	/* Init the context */
	clear_bit(__ST_FLAG_ON, __ST_FLAGS);
	atomic_set(&st_cxt->hack_ready_on_cpu, 0);
	atomic_set(&st_cxt->crit_req, 0);
	atomic_set(&st_cxt->crit_sleep, 0);
	atomic_set(&st_cxt->crit_avail, __ST_BASE_CRIT_LIMIT);
	atomic_set(&st_cxt->crit_limit, __ST_BASE_CRIT_LIMIT);
	init_waitqueue_head(&st_cxt->critical_sleeping_wq);
	mutex_init(&st_cxt->registry_mutex);

	ret = try_hack_search();
	if (ret != 0)
		return ret;

	ret = load_metrics();
	if (ret != 0)
		goto failed_load_metrics;

	ret = load_monitor();
	if (ret != 0)
		goto failed_load_monitor;

	ret = load_throttle();
	if (ret != 0)
		goto failed_load_throttle;

	ret = load_timer();
	if (ret != 0)
		goto failed_load_timer;

	ret = load_driver();
	if (ret != 0)
		goto failed_load_driver;

	ret = load_metrics_driver();
	if (ret != 0)
		goto failed_load_metrics_driver;

	pr_info("%s: module correctly loaded\n", __ST_MODNAME);

	return 0;

failed_load_metrics_driver:
	unload_metrics_driver();
failed_load_driver:
	unload_timer();
failed_load_timer:
	unload_throttle();
failed_load_throttle:
	unload_monitor();
failed_load_monitor:
	unload_metrics();
failed_load_metrics:

	return ret;
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
	clear_bit(__ST_FLAG_ON, __ST_FLAGS);

	/* Unload the drivers */
	unload_metrics_driver();
	unload_driver();

	/* Delete the timer */
	unload_timer();

	/* Unload the throttling mechanism */
	unload_throttle();

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
