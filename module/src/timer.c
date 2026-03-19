#include "syscall-throttle.h"

/*
  The timer callback executed periodically:
    1. Calls the update_limit_and_wake routine
    2. Re-arms the timer
*/
static void timer_callback(struct timer_list *t)
{
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
	timer_setup(&sys_thr_cxt->periodic_timer, timer_callback, 0);
	mod_timer(&sys_thr_cxt->periodic_timer,
		  jiffies + msecs_to_jiffies(TIMER_INTERVAL));
	return 0;
}