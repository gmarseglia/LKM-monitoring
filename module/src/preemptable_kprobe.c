#include "syscall-throttle.h"

noinline void dummy_run(void *arg);

/*
  Dummy function to be run on each CPU, that allow the pre_handler_search to be
  run inside a kprobe
*/
noinline void dummy_run(void *arg)
{
	asm volatile(""); // force GCC to call the block
	return;
}

/*
  Pre-handler for searching the position of the kprobe context in the per-CPU
  memory and saving it in the 'saved_kprobe_context_p' per-CPU variable
*/
static int __kprobes pre_handler_search(struct kprobe *p, struct pt_regs *regs)
{
	__ST_LOG_FINE pr_info("%s: pre_handler_search running on CPU %d",
			      __ST_MODNAME, smp_processor_id());

	/* Brute force search for the position of the kprobe context */
	struct kprobe *temp;
	unsigned long temp_offset = 0;
	while (copy_from_kernel_nofault(&temp,
					this_cpu_ptr((void *)temp_offset),
					sizeof(struct kprobe *)) != -EFAULT) {
		/* Check if *temp_ptr points at the variable representing the
		 * kprobe context  */
		if (temp == p) {
			st_cxt->saved_kprobe_ctx_offset = temp_offset;
			atomic_inc(&st_cxt->hack_ready_on_cpu);
			return 0;
		}
		temp_offset++;
	}

	pr_err("%s: NOT found on CPU %d", __ST_MODNAME, smp_processor_id());

	return 0;
}

/*
  Performs the preparatory steps for the preemptable kprobe hack:
    1. Register a kprobe to be run on each CPU
    2. Run pre_handler_search on each CPU
    3. On each CPUs, the pre-handler saves the position of the kprobe context
    4. Unregister the kprobe
*/
int load_hack_search(void)
{
	int ret;

	/* Initialize and register the search probe */
	struct kprobe search_probe;
	search_probe.addr = (kprobe_opcode_t *)dummy_run;
	search_probe.pre_handler = pre_handler_search;

	ret = register_kprobe(&search_probe);
	if (ret < 0) {
		pr_err("%s: register_kprobe failed, returned %d\n",
		       __ST_MODNAME, ret);
		return ret;
	}

	/* Execute "dummy_run" to trigger the search probe pre-handler */
	dummy_run(NULL);

	/* Unregister the search probe */
	unregister_kprobe(&search_probe);

	if (atomic_read(&st_cxt->hack_ready_on_cpu) != 1) {
		pr_err("%s: load_hack_search did not complete on every CPU.",
		       __ST_MODNAME);
		return -1;
	} else {
		__ST_LOG pr_info("%s: load_hack_search completed.",
				 __ST_MODNAME);
		return 0;
	}
}