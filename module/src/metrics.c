#include "syscall-throttle.h"

static struct syscall_throttle_sleep_metrics __st_slp_met;
struct syscall_throttle_sleep_metrics *st_slp_met = &__st_slp_met;
DEFINE_PER_CPU(struct syscall_throttle_delay_metrics, st_dly_met);

void update_sleep_metrics(void)
{
	int curr_sleep;

	if (!__ST_IS_ON)
		return;

	curr_sleep = atomic_read(&st_cxt->crit_sleep);

	spin_lock_bh(&st_slp_met->lock); // #TODO: investigate more on _bh

	st_slp_met->units_passed++;
	st_slp_met->max_sleep = MAX(curr_sleep, st_slp_met->max_sleep);
	st_slp_met->total_sleep += curr_sleep;
	st_slp_met->last_sleep = curr_sleep;

	spin_unlock_bh(&st_slp_met->lock);
}

int load_metrics(void)
{
	/* Initialize the lock */
	spin_lock_init(&st_slp_met->lock);

	/* Allocate and initialize the structs for delay metrics */
	int cpu;
	struct syscall_throttle_delay_metrics *dm;
	for_each_possible_cpu(cpu)
	{
		dm = per_cpu_ptr(&st_dly_met, cpu);
		seqcount_init(&dm->count);
	}

	return 0;
}

void unload_metrics(void) {}