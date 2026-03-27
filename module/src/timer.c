#include "syscall-throttle.h"

/*
  The timer callback executed periodically:
    1. Calls the update_limit_and_wake routine
    2. Re-arms the timer
*/
static void timer_callback(struct timer_list *t)
{
	update_sleep_metrics();

	update_limit_and_wake();

	/* Re-arm the timer to fire again in __ST_TIMER_INTERVAL milliseconds */
	mod_timer(&st_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(__ST_TIMER_INTERVAL));
}

/*
  Load the periodic timer
*/
int load_timer(void)
{
	/* Setup first execution of the timer routine */
	timer_setup(&st_cxt->periodic_timer, timer_callback, 0);
	mod_timer(&st_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(__ST_TIMER_INTERVAL));

	return 0;
}

/* Un load the timer */
void unload_timer(void)
{
	timer_delete_sync(&st_cxt->periodic_timer);

	pr_info("%s: timer unregistered\n", __ST_MODNAME);
}