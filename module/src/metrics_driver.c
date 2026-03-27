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

	struct rhashtable *ht = pde_data(inode);
	state->ht = ht;
	rhashtable_walk_enter(state->ht, &state->iter);

	return 0;
}

static void *rhash_seq_start(struct seq_file *m, loff_t *pos)
{
	struct rhash_seq_state *state = m->private;

	pr_info("START HASH");

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

struct bitmap_seq_state {
	unsigned long *bitmap;
	long long curr_pos;
	long long max;
};

static void *bitmap_seq_start(struct seq_file *m, loff_t *pos);
static void *bitmap_seq_next(struct seq_file *m, void *v, loff_t *pos);
static void bitmap_seq_stop(struct seq_file *m, void *v);
static int bitmap_seq_show(struct seq_file *m, void *v);
static int bitmap_seq_open(struct inode *inode, struct file *file);

static struct seq_operations bitmap_seq_ops = {
	.start = bitmap_seq_start,
	.next = bitmap_seq_next,
	.stop = bitmap_seq_stop,
	.show = bitmap_seq_show,
};

static struct proc_ops bitmap_proc_ops = {.proc_open = bitmap_seq_open,
					  .proc_read = seq_read,
					  .proc_lseek = seq_lseek,
					  .proc_release = seq_release_private};

static int bitmap_seq_open(struct inode *inode, struct file *file)
{
	struct bitmap_seq_state *state = __seq_open_private(
		file, &bitmap_seq_ops, sizeof(struct bitmap_seq_state));
	if (!state)
		return -ENOMEM;

	unsigned long **ptr = pde_data(inode);
	state->bitmap = *ptr;
	state->max = __ST_MAX_NR;

	return 0;
}

static void *bitmap_seq_start(struct seq_file *m, loff_t *pos)
{
	struct bitmap_seq_state *state = m->private;

	state->curr_pos = *pos;
	if (*pos >= 0 && *pos < state->max) {
		return (void *)state;
	} else {
		return NULL;
	}
}

static void *bitmap_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct bitmap_seq_state *state = m->private;

	(*pos)++;
	state->curr_pos = *pos;
	if (*pos >= 0 && *pos < state->max) {
		return (void *)state;
	} else {
		return NULL;
	}
}

static void bitmap_seq_stop(struct seq_file *m, void *v) { return; }

static int bitmap_seq_show(struct seq_file *m, void *v)
{
	struct bitmap_seq_state *state = m->private;

	long long pos = state->curr_pos;

	if (!IS_ERR_OR_NULL(v) && pos >= 0 && pos < state->max) {
		if (test_bit(pos, state->bitmap) == false)
			return 0;

		seq_printf(m, "nr:%lld\n", pos);
	}

	return 0;
}

int load_metrics_driver(void)
{
	st_cxt->my_proc_dir = proc_mkdir(__ST_MODNAME, NULL);
	if (!st_cxt->my_proc_dir)
		return -ENOMEM;

	proc_create_data("prog_names", 0444, st_cxt->my_proc_dir,
			 &rhash_proc_ops, &st_cxt->prog_names_registry);

	proc_create_data("nr", 0444, st_cxt->my_proc_dir, &bitmap_proc_ops,
			 &st_cxt->sys_numbers_registry);

	return 0;
}

void unload_metrics_driver(void)
{
	remove_proc_entry("prog_names", st_cxt->my_proc_dir);
	remove_proc_entry("nr", st_cxt->my_proc_dir);
	remove_proc_entry(__ST_MODNAME, NULL);
}