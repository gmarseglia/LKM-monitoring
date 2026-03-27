#include "syscall-throttle.h"

struct my_seq_state {
	void *tmp;
	struct rhashtable *ht;
	struct rhashtable_iter iter;
	struct string_entry *last_entry;
};

static void *my_seq_start(struct seq_file *m, loff_t *pos);
static void *my_seq_next(struct seq_file *m, void *v, loff_t *pos);
static void my_seq_stop(struct seq_file *m, void *v);
static int my_seq_show(struct seq_file *m, void *v);
static int my_seq_open(struct inode *inode, struct file *file);
static int my_seq_release(struct inode *inode, struct file *file);

static struct proc_dir_entry *my_proc_dir;

static struct seq_operations my_seq_ops = {
	.start = my_seq_start,
	.next = my_seq_next,
	.stop = my_seq_stop,
	.show = my_seq_show,
};

static void *my_seq_start(struct seq_file *m, loff_t *pos)
{
	struct my_seq_state *state = m->private;

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

static void *my_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct my_seq_state *state = m->private;

	/* Increment pos to signal not start */
	*pos = 1;

	do {
		state->last_entry = rhashtable_walk_next(&state->iter);
	} while (IS_ERR(state->last_entry) &&
		 PTR_ERR(state->last_entry) == -EAGAIN);

	return state->last_entry;
}

static void my_seq_stop(struct seq_file *m, void *v)
{
	struct my_seq_state *state = m->private;

	rhashtable_walk_stop(&state->iter);
}

static int my_seq_show(struct seq_file *m, void *v)
{
	struct string_entry *entry = v;

	// Safety check: skip if we somehow got an error pointer
	if (IS_ERR_OR_NULL(v))
		return 0;

	seq_printf(m, "key:%s\n", entry->string_key);
	return 0;
}

static struct proc_ops my_proc_ops = {.proc_open = my_seq_open,
				      .proc_read = seq_read,
				      .proc_lseek = seq_lseek,
				      .proc_release = my_seq_release};

static int my_seq_open(struct inode *inode, struct file *file)
{
	struct my_seq_state *state;

	state = __seq_open_private(file, &my_seq_ops, sizeof(*state));
	if (!state)
		return -ENOMEM;

	struct rhashtable *ht = pde_data(inode);
	state->ht = ht;
	rhashtable_walk_enter(state->ht, &state->iter);

	return 0;
}

static int my_seq_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;
	struct my_seq_state *state = m->private;

	rhashtable_walk_exit(&state->iter);

	return seq_release_private(inode, file);
}

int load_metrics_driver(void)
{
	my_proc_dir = proc_mkdir(__ST_MODNAME, NULL);
	if (!my_proc_dir)
		return -ENOMEM;

	proc_create_data("prog_names", 0444, my_proc_dir, &my_proc_ops,
			 &st_cxt->prog_names_registry);

	return 0;
}

void unload_metrics_driver(void)
{
	remove_proc_entry("table_a", my_proc_dir);
	remove_proc_entry(__ST_MODNAME, NULL);
}