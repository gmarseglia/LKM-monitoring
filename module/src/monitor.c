#include "syscall-throttle.h"

struct string_entry {
	char string_key[__ST_MAX_STR_LEN];
	struct rhash_head linkage;
	struct rcu_head rcu;
};

static u32 my_string_hashfn(const void *data, u32 len, u32 seed)
{
	return jhash((char *)data, strlen((char *)data), seed);
}

static int my_string_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	char *search_key = (char *)arg->key;
	struct string_entry *entry = (struct string_entry *)obj;

	/* Returns 0 on an exact match (just like strcmp) */
	return strcmp(search_key, entry->string_key);
}

static void my_registry_free_fn(void *ptr, void *arg)
{
	/* At this point, the table is being destroyed and no new readers
	 * can access it, so it is safe to just kfree the memory directly. */
	kfree((struct string_entry *)ptr);
}

static struct rhashtable_params registry_params = {
	.head_offset = offsetof(struct string_entry, linkage),
	.key_offset = offsetof(struct string_entry, string_key),
	.key_len = __ST_MAX_STR_LEN,
	.hashfn = my_string_hashfn,
	.obj_cmpfn = my_string_cmpfn,
	.automatic_shrinking = true,
};

int register_critical_str(char *new_str, struct rhashtable *ht)
{
	struct string_entry *entry;
	int err;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	/* Safely copy the string into our structure */
	strscpy(entry->string_key, new_str, sizeof(entry->string_key));

	/* Insert it. The kernel handles the bucket locking internally. */
	err = rhashtable_insert_fast(ht, &entry->linkage, registry_params);
	if (err) {
		kfree(entry);
		return err;
	}

	return 0;
}

int unregister_critical_str(char *target_str, struct rhashtable *ht)
{
	struct string_entry *entry;

	rcu_read_lock();
	entry = rhashtable_lookup_fast(ht, target_str, registry_params);
	rcu_read_unlock();

	if (!entry)
		return 0; /* Not found */

	if (rhashtable_remove_fast(ht, &entry->linkage, registry_params) == 0) {
		kfree_rcu(entry, rcu);
	}

	return 0;
}

static bool is_registered_str(char *search_str, struct rhashtable *ht)
{
	struct string_entry *entry;
	bool found = false;

	/* Enter the RCU read-side critical section */
	rcu_read_lock();

	entry = rhashtable_lookup_fast(ht, search_str, registry_params);
	if (entry) {
		found = true;
	}

	/* Exit the RCU read-side critical section */
	rcu_read_unlock();

	return found;
}

int register_critical_num(unsigned int nr)
{
	// #TODO: add limit check?
	set_bit(nr, st_cxt->sys_numbers_registry);
	return 0;
}

int unregister_critical_num(unsigned int nr)
{
	// #TODO: add limit check?
	clear_bit(nr, st_cxt->sys_numbers_registry);
	return 0;
}

/*
  Checks if the syscall request is critical
*/
int is_critical(int nr)
{
	/* Check syscall */

	if (!test_bit(nr, st_cxt->sys_numbers_registry))
		return 0;

	/* Get PID */
	char pid[64];
	snprintf(pid, sizeof(pid), "%d", current->pid);

	/* Check PID */
	bool pid_found = is_registered_str(pid, &st_cxt->pids_registry);
	if (pid_found)
		return true;

	/* Get eUID */
	char euid[64];
	kuid_t current_kuid = current_euid();
	uid_t euid_val = from_kuid(&init_user_ns, current_kuid);
	snprintf(euid, sizeof(euid), "%u", euid_val);

	/* Check eUID */
	bool euid_found = is_registered_str(euid, &st_cxt->euid_registry);
	if (euid_found)
		return true;

	/* Check program name */
	bool name_found =
		is_registered_str(current->comm, &st_cxt->prog_names_registry);
	if (name_found)
		return true;

	return false;
}

int load_monitor(void)
{
	int ret;

	st_cxt->sys_numbers_registry = bitmap_zalloc(MAX_NR, GFP_KERNEL);

	ret = rhashtable_init(&st_cxt->pids_registry, &registry_params);
	if (ret < 0) {
		pr_err("%s: rhashtable_init failed with err=%d", MODNAME, ret);
		return ret;
	}

	ret = rhashtable_init(&st_cxt->euid_registry, &registry_params);
	if (ret < 0) {
		pr_err("%s: rhashtable_init failed with err=%d", MODNAME, ret);
		return ret;
	}

	ret = rhashtable_init(&st_cxt->prog_names_registry, &registry_params);
	if (ret < 0) {
		pr_err("%s: rhashtable_init failed with err=%d", MODNAME, ret);
		return ret;
	}

	return 0;
}

void unload_monitor(void)
{
	bitmap_free(st_cxt->sys_numbers_registry);

	rhashtable_free_and_destroy(&st_cxt->pids_registry, my_registry_free_fn,
				    NULL);

	rhashtable_free_and_destroy(&st_cxt->euid_registry, my_registry_free_fn,
				    NULL);

	rhashtable_free_and_destroy(&st_cxt->prog_names_registry,
				    my_registry_free_fn, NULL);

	rcu_barrier();
}