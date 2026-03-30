#include "linux/proc_fs.h"
#include "linux/seq_file.h"
#include "syscall-throttle.h"

struct rhash_seq_state {
	struct rhashtable *ht;
	struct rhashtable_iter iter;
	struct string_entry *last_entry;
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

	state = __seq_open_private(file, &rhash_seq_ops, sizeof(*state));
	if (!state)
		return -ENOMEM;
	state->ht = pde_data(inode);

	rhashtable_walk_enter(state->ht, &state->iter);

	return 0;
}

static void *rhash_seq_start(struct seq_file *m, loff_t *pos)
{
	struct rhash_seq_state *state = m->private;

	/* Reset the walk */
	if (*pos == 0) {
		rhashtable_walk_exit(&state->iter);
		rhashtable_walk_enter(state->ht, &state->iter);
		state->last_entry = NULL;
	}

	rhashtable_walk_start(&state->iter);

	// If new read, then get first element
	if (*pos == 0) {
		do {
			state->last_entry = rhashtable_walk_next(&state->iter);
		} while (IS_ERR(state->last_entry) &&
			 PTR_ERR(state->last_entry) == -EAGAIN);
	}

	return state->last_entry;
}

static void *rhash_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct rhash_seq_state *state = m->private;

	/* Increment pos to signal not start */
	(*pos)++;

	do {
		state->last_entry = rhashtable_walk_next(&state->iter);
	} while (IS_ERR(state->last_entry) &&
		 PTR_ERR(state->last_entry) == -EAGAIN);

	return state->last_entry;
}

static void rhash_seq_stop(struct seq_file *m, void *v)
{
	struct rhash_seq_state *state = m->private;

	rhashtable_walk_stop(&state->iter);
}

static int rhash_seq_show(struct seq_file *m, void *v)
{
	struct string_entry *entry = v;

	// Safety check: skip if we somehow got an error pointer
	if (IS_ERR_OR_NULL(v))
		return 0;

	seq_printf(m, "key:%s\n", entry->string_key);
	return 0;
}

static int rhash_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct rhash_seq_state *state = m->private;

	rhashtable_walk_exit(&state->iter);

	return seq_release_private(inode, file);
}

static int nr_show(struct seq_file *m, void *v)
{
	for (int nr = 0; nr < __ST_MAX_NR; nr++) {
		if (test_bit(nr, st_cxt->nr_registry) == true) {
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
		   st_slp_met->avg_sleep / __ST_METRICS_SCALING_FACTOR,
		   st_slp_met->max_sleep / __ST_METRICS_SCALING_FACTOR);

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
	seq_printf(m, "throttle_running=%d\nlimit=%d\n",
		   atomic_read(&st_cxt->throttle_running),
		   atomic_read(&st_cxt->crit_limit));
	return 0;
}

int load_metrics_driver(void)
{
	st_cxt->my_proc_dir = proc_mkdir(__ST_MODNAME, NULL);
	if (!st_cxt->my_proc_dir)
		return -ENOMEM;

	// #TODO: add return control
	proc_create_data("program_names", 0444, st_cxt->my_proc_dir,
			 &rhash_proc_ops, &st_cxt->prog_names_registry);

	proc_create_data("euid", 0444, st_cxt->my_proc_dir, &rhash_proc_ops,
			 &st_cxt->euid_registry);

	proc_create_single("nr", 0444, st_cxt->my_proc_dir, nr_show);

	proc_create_single("sleep_metrics", 0444, st_cxt->my_proc_dir,
			   sleep_metrics_show);

	proc_create_single("delay_metrics", 0444, st_cxt->my_proc_dir,
			   delay_metrics_show);

	proc_create_single("config", 0444, st_cxt->my_proc_dir, config_show);

	return 0;
}

void unload_metrics_driver(void)
{
	remove_proc_entry("program_names", st_cxt->my_proc_dir);
	remove_proc_entry("euid", st_cxt->my_proc_dir);
	remove_proc_entry("nr", st_cxt->my_proc_dir);
	remove_proc_entry("sleep_metrics", st_cxt->my_proc_dir);
	remove_proc_entry("delay_metrics", st_cxt->my_proc_dir);
	remove_proc_entry("config", st_cxt->my_proc_dir);
	remove_proc_entry(__ST_MODNAME, NULL);
}