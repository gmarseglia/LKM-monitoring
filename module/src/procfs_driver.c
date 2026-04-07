#include "syscall-throttle.h"

struct rhash_seq_state {
	struct rhashtable_iter iter;
};

static int rhash_seq_open(struct inode *inode, struct file *file);
static void *rhash_seq_start(struct seq_file *m, loff_t *pos);
static void *rhash_seq_next(struct seq_file *m, void *v, loff_t *pos);
static void rhash_seq_stop(struct seq_file *m, void *v);
static int rhash_seq_show(struct seq_file *m, void *v);
static int rhash_seq_release(struct inode *inode, struct file *file);

static struct seq_operations rhash_seq_ops = {
	.start = rhash_seq_start,
	.next = rhash_seq_next,
	.stop = rhash_seq_stop,
	.show = rhash_seq_show,
};

static struct proc_ops rhash_proc_ops = {.proc_open = rhash_seq_open,
					 .proc_read = seq_read,
					 .proc_lseek = seq_lseek,
					 .proc_release = rhash_seq_release};

static int rhash_seq_open(struct inode *inode, struct file *file)
{
	struct rhash_seq_state *state;
	struct rhashtable *ht;

	/* Allocate state */
	state = __seq_open_private(file, &rhash_seq_ops, sizeof(*state));
	if (!state)
		return -ENOMEM;

	/* Initialize iterator */
	ht = pde_data(inode);
	rhashtable_walk_enter(ht, &state->iter);

	return 0;
}

static void *rhash_seq_start(struct seq_file *m, loff_t *pos)
{
	struct rhash_seq_state *state = m->private;
	struct string_entry *next_entry;

	/* This takes the RCU read lock */
	rhashtable_walk_start(&state->iter);

	/* Advance to next element and return */
	do {
		(*pos)++; // Increment *pos to indicate advance
		next_entry = rhashtable_walk_next(&state->iter);
	} while (IS_ERR(next_entry) && PTR_ERR(next_entry) == -EAGAIN);

	return next_entry;
}

static void *rhash_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct rhash_seq_state *state = m->private;
	struct string_entry *next_entry;

	/* Advance to next element and return */

	do {
		(*pos)++; // Increment *pos to indicate advance
		next_entry = rhashtable_walk_next(&state->iter);
	} while (IS_ERR(next_entry) && PTR_ERR(next_entry) == -EAGAIN);

	return next_entry;
}

static int rhash_seq_show(struct seq_file *m, void *v)
{
	struct string_entry *entry = v;

	/* Print if possible */
	if (!IS_ERR_OR_NULL(v))
		seq_printf(m, "key:%s\n", entry->string_key);

	return 0;
}

static void rhash_seq_stop(struct seq_file *m, void *v)
{
	struct rhash_seq_state *state = m->private;

	/* Release the RCU read lock */
	rhashtable_walk_stop(&state->iter);
}

static int rhash_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct rhash_seq_state *state = m->private;

	/* Close the iterator */
	rhashtable_walk_exit(&state->iter);

	return seq_release_private(inode, file);
}

static int nr_show(struct seq_file *m, void *v)
{
	for (int nr = 0; nr < __ST_MAX_NR; nr++) {
		if (test_bit(nr, st_cxt->nr_registry)) {
			seq_printf(m, "nr:%d\n", nr);
		}
	}
	return 0;
}

static int sleep_metrics_show(struct seq_file *m, void *v)
{
	unsigned long avg_sleep, max_sleep;

	spin_lock(&st_slp_met->lock);
	avg_sleep = st_slp_met->avg_sleep;
	max_sleep = st_slp_met->max_sleep;
	spin_unlock(&st_slp_met->lock);

	seq_printf(m, "avg_sleep=%ld\nmax_sleep=%ld\n",
		   avg_sleep / __ST_METRICS_SCALING_FACTOR,
		   max_sleep / __ST_METRICS_SCALING_FACTOR);

	return 0;
}

static int delay_metrics_show(struct seq_file *m, void *v)
{
	int cpu;
	unsigned int seq;
	struct syscall_throttle_delay_metrics max_global_dm;
	struct syscall_throttle_delay_metrics max_local_dm;
	struct syscall_throttle_delay_metrics *cpu_dm;

	/* Find the global max from the local max */
	max_global_dm.max_delay_ms = -1;
	for_each_possible_cpu(cpu)
	{
		do {
			cpu_dm = per_cpu_ptr(&st_dly_met, cpu);
			seq = read_seqcount_begin(&cpu_dm->count);
			memcpy(&max_local_dm, cpu_dm,
			       sizeof(struct syscall_throttle_delay_metrics));
		} while (read_seqcount_retry(&cpu_dm->count, seq));

		if (max_local_dm.max_delay_ms > max_global_dm.max_delay_ms) {
			memcpy(&max_global_dm, &max_local_dm,
			       sizeof(struct syscall_throttle_delay_metrics));
		}
	}

	seq_printf(
		m,
		"nr=%d\ntype=%s\neuid=%s\nprogram_name=%s\ndelay_in_ms=%lld\n",
		max_global_dm.qr.nr, max_global_dm.qr.type,
		max_global_dm.qr.euid, max_global_dm.qr.name,
		max_global_dm.max_delay_ms);

	return 0;
}

static int config_show(struct seq_file *m, void *v)
{
	seq_printf(m, "throttle_running=%d\nlimit=%d\n", __ST_IS_ON ? 1 : 0,
		   atomic_read(&st_cxt->crit_limit));
	return 0;
}

int load_metrics_driver(void)
{
	void *ret;

	/* Create the proc directory */
	st_cxt->proc_dir = proc_mkdir(__ST_MODNAME, NULL);
	if (!st_cxt->proc_dir)
		goto proc_mkdir_error;

	ret = proc_create_data("program_names", 0444, st_cxt->proc_dir,
			       &rhash_proc_ops, &st_cxt->prog_names_registry);
	if (!ret)
		goto program_names_error;

	ret = proc_create_data("euid", 0444, st_cxt->proc_dir, &rhash_proc_ops,
			       &st_cxt->euid_registry);
	if (!ret)
		goto euid_error;

	ret = proc_create_single("nr", 0444, st_cxt->proc_dir, nr_show);
	if (!ret)
		goto nr_error;

	ret = proc_create_single("sleep_metrics", 0444, st_cxt->proc_dir,
				 sleep_metrics_show);
	if (!ret)
		goto sleep_metrics_error;

	ret = proc_create_single("delay_metrics", 0444, st_cxt->proc_dir,
				 delay_metrics_show);
	if (!ret)
		goto delay_metrics_error;

	ret = proc_create_single("config", 0444, st_cxt->proc_dir, config_show);
	if (!ret)
		goto config_error;

	return 0;

config_error:
	remove_proc_entry("delay_metrics", st_cxt->proc_dir);
delay_metrics_error:
	remove_proc_entry("sleep_metrics", st_cxt->proc_dir);
sleep_metrics_error:
	remove_proc_entry("nr", st_cxt->proc_dir);
nr_error:
	remove_proc_entry("euid", st_cxt->proc_dir);
euid_error:
	remove_proc_entry("program_names", st_cxt->proc_dir);
program_names_error:
	remove_proc_entry(__ST_MODNAME, NULL);
proc_mkdir_error:
	return -ENOMEM;
}

void unload_metrics_driver(void)
{
	remove_proc_entry("config", st_cxt->proc_dir);
	remove_proc_entry("delay_metrics", st_cxt->proc_dir);
	remove_proc_entry("sleep_metrics", st_cxt->proc_dir);
	remove_proc_entry("nr", st_cxt->proc_dir);
	remove_proc_entry("euid", st_cxt->proc_dir);
	remove_proc_entry("program_names", st_cxt->proc_dir);
	remove_proc_entry(__ST_MODNAME, NULL);
}