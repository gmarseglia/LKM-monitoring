#include "syscall-throttle.h"

struct syscall_throttle_sleep_metrics *st_slp_met = NULL;
struct syscall_throttle_delay_metrics *st_dly_met = NULL;

void update_sleep_metrics(void)
{
	if (atomic_read(&st_cxt->throttle_running) == false)
		return;

	spin_lock_bh(&st_slp_met->lock); // #TODO: investigate more on _bh

	int curr_sleep =
		atomic_read(&st_cxt->crit_sleep) * __ST_METRICS_SCALING_FACTOR;

	st_slp_met->max_sleep = MAX(curr_sleep, st_slp_met->max_sleep);

	unsigned long total_sleep =
		st_slp_met->avg_sleep * st_slp_met->units_passed;

	st_slp_met->units_passed++;
	st_slp_met->avg_sleep =
		(total_sleep + curr_sleep) / st_slp_met->units_passed;

	pr_info("%s: max_sleep=%lu, avg_sleep=%lu, units_passed=%lu\n",
		__ST_MODNAME,
		st_slp_met->max_sleep / __ST_METRICS_SCALING_FACTOR,
		st_slp_met->avg_sleep / __ST_METRICS_SCALING_FACTOR,
		st_slp_met->units_passed);

	spin_unlock_bh(&st_slp_met->lock);
}

int load_metrics(void)
{
	/* Allocate the struct for sleep metrics */
	st_slp_met = kzalloc(sizeof(struct syscall_throttle_sleep_metrics),
			     GFP_KERNEL);
	if (!st_slp_met) {
		pr_err("%s: failed to allocate memory for "
		       "syscall_throttle_metrics",
		       __ST_MODNAME);
		return -ENOMEM;
	}

	/* Initialize the lock */
	spin_lock_init(&st_slp_met->lock);

	/* Allocate and initialize the structs for delay metrics */
	st_dly_met = kzalloc(sizeof(struct syscall_throttle_delay_metrics) *
				     __ST_MAX_NR,
			     GFP_KERNEL);
	if (!st_dly_met) {
		pr_err("%s: failed to allocate memory for "
		       "syscall_throttle_delay_metrics",
		       __ST_MODNAME);
		return -ENOMEM;
	}

	for (int nr = 0; nr < __ST_MAX_NR; nr++) {
		spin_lock_init(&(st_dly_met[nr].lock));
	}

	return 0;
}

static void print_metrics(void)
{
	for (int nr = 0; nr < 10; nr++) {
		struct syscall_throttle_delay_metrics *dm = &st_dly_met[nr];

		spin_lock(&dm->lock);

		pr_info("nr:%d, euid:%s, name:%s, type:%s -> max_delay=%lld ms",
			dm->qr.nr, dm->qr.euid, dm->qr.name, dm->qr.type,
			dm->max_delay_ms);

		spin_unlock(&dm->lock);
	}
}

void unload_metrics(void)
{
	print_metrics();
	kfree(&st_slp_met);
	kfree(st_dly_met);
}