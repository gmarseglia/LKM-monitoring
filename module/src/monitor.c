#include "syscall-throttle.h"

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
	int ret;

	mutex_lock(&st_cxt->registry_mutex);

	rcu_read_lock();
	entry = rhashtable_lookup_fast(ht, new_str, registry_params);
	rcu_read_unlock();

	/* If found, then skip insert */
	if (entry) {
		ret = 0;
		goto end_register;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto end_register;
	}

	/* Safely copy the string into our structure */
	strscpy(entry->string_key, new_str, sizeof(entry->string_key));

	/* Insert it. The kernel handles the bucket locking internally. */
	ret = rhashtable_insert_fast(ht, &entry->linkage, registry_params);
	if (ret) {
		kfree(entry);
		goto end_register;
	}

end_register:
	mutex_unlock(&st_cxt->registry_mutex);
	return ret;
}

int unregister_critical_str(char *target_str, struct rhashtable *ht)
{
	struct string_entry *entry;
	int ret;

	mutex_lock(&st_cxt->registry_mutex);

	rcu_read_lock();
	entry = rhashtable_lookup_fast(ht, target_str, registry_params);
	rcu_read_unlock();

	/* Not found */
	if (!entry) {
		ret = 0;
		goto end_unregister;
	}

	if (rhashtable_remove_fast(ht, &entry->linkage, registry_params) == 0) {
		kfree_rcu(entry, rcu);
	}

end_unregister:
	mutex_unlock(&st_cxt->registry_mutex);
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
	set_bit(nr, st_cxt->nr_registry);
	return 0;
}

int unregister_critical_num(unsigned int nr)
{
	// #TODO: add limit check?
	clear_bit(nr, st_cxt->nr_registry);
	return 0;
}

/*
  Checks if the syscall request is critical
*/
inline bool is_critical(struct syscall_throttle_query_result *st_qr)
{
	st_qr->is_critical = false;

	/* Check syscall */
	if (!test_bit(st_qr->nr, st_cxt->nr_registry)) {
		return false;
	}

	/* Get eUID */
	kuid_t current_kuid = current_euid();
	uid_t euid_val = from_kuid(&init_user_ns, current_kuid);
	snprintf(st_qr->euid, __ST_MAX_STR_LEN, "%u", euid_val);

	/* Check eUID */
	if (!st_qr->is_critical) {
		if (is_registered_str(st_qr->euid, &st_cxt->euid_registry)) {
			st_qr->is_critical = true;
			st_qr->type = "EUID";
		}
	}

	/* Get program name */
	snprintf(st_qr->name, __ST_MAX_STR_LEN, "%s", current->comm);
	/* Check program name */
	if (!st_qr->is_critical) {
		if (is_registered_str(st_qr->name,
				      &st_cxt->prog_names_registry)) {
			st_qr->is_critical = true;
			st_qr->type = "NAME";
		}
	}

	return st_qr->is_critical;
}

int load_monitor(void)
{
	int ret;

	st_cxt->nr_registry = bitmap_zalloc(__ST_MAX_NR, GFP_KERNEL);

	ret = rhashtable_init(&st_cxt->euid_registry, &registry_params);
	if (ret < 0) {
		pr_err("%s: rhashtable_init failed with err=%d", __ST_MODNAME,
		       ret);
		return ret;
	}

	ret = rhashtable_init(&st_cxt->prog_names_registry, &registry_params);
	if (ret < 0) {
		pr_err("%s: rhashtable_init failed with err=%d", __ST_MODNAME,
		       ret);
		return ret;
	}

	return 0;
}

void unload_monitor(void)
{
	bitmap_free(st_cxt->nr_registry);

	rhashtable_free_and_destroy(&st_cxt->euid_registry, my_registry_free_fn,
				    NULL);

	rhashtable_free_and_destroy(&st_cxt->prog_names_registry,
				    my_registry_free_fn, NULL);

	rcu_barrier(); // #TODO: investigate if rcu_barrier() is needed in other
		       // places
}