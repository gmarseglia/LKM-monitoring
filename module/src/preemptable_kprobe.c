#include "syscall-throttle.h"

/* Dummy function to be run on a CPU, that allow the pre_handler_search to be
 * run inside a kprobe */
static noinline void dummy_run(void *arg)
{
	barrier(); // force GCC to call the block
	return;
}

/* Pre-handler for searching the offset in the per-CPU memory corresponding to
 * the kprobe context */
static int __kprobes pre_handler_search(struct kprobe *p, struct pt_regs *regs)
{
	pr_debug("%s: pre_handler_search running on CPU %d\n", __ST_MODNAME,
		 smp_processor_id());

	/* Brute force search for the position of the kprobe context */
	struct kprobe **temp_offset = 0;
	struct kprobe *temp;
	while (copy_from_kernel_nofault(&temp, this_cpu_ptr(temp_offset),
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

	pr_err("%s: NOT found on CPU %d\n", __ST_MODNAME, smp_processor_id());

	return 0;
}

/* Performs the preparatory steps for the preemptable kprobe hack:
 * 1. Register a kprobe to be run on each CPU
 * 2. Run pre_handler_search on each CPU
 * 3. On each CPUs, the pre-handler saves the position of the kprobe context
 * 4. Unregister the kprobe*/
int try_hack_search(void)
{
	int ret;
	struct kprobe search_probe;

	/* Initialize and register the search probe */
	search_probe.addr = (kprobe_opcode_t *)dummy_run;
	search_probe.pre_handler = pre_handler_search;

	ret = register_kprobe(&search_probe);
	if (ret < 0) {
		pr_err("%s: register_kprobe on \"dummy_run\" failed, returned "
		       "%d\n",
		       __ST_MODNAME, ret);
		return ret;
	}

	/* Execute "dummy_run" to trigger the search probe pre-handler */
	dummy_run(NULL);

	/* Unregister the search probe */
	unregister_kprobe(&search_probe);

	if (atomic_read(&st_cxt->hack_ready_on_cpu) != 1) {
		pr_err("%s: load_hack_search failed.\n", __ST_MODNAME);
		return -ENOENT;
	} else {
		pr_notice("%s: load_hack_search completed.\n", __ST_MODNAME);
		return 0;
	}
}