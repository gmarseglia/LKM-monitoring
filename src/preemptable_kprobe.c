#include "syscall-throttle.h"

/*
  Dummy function to be run on each CPU, that allow the pre_handler_search to be
  run inside a kprobe
*/
static void dummy_run(void *arg)
{
	LOG_FINEST pr_info("%s: dummy running on CPU %d", MODNAME,
			   smp_processor_id());
	return;
}

/*
  Pre-handler for searching the position of the kprobe context in the per-CPU
  memory and saving it in the 'saved_kprobe_context_p' per-CPU variable
*/
static int __kprobes pre_handler_search(struct kprobe *p, struct pt_regs *regs)
{

	LOG_FINE pr_info("%s: pre_handler_search running on CPU %d", MODNAME,
			 smp_processor_id());

	/* Brute force search for the position of the kprobe context */
	struct kprobe **temp = (struct kprobe **)&saved_kprobe_context_p;
	while ((unsigned long)temp > 0) {
		temp--;
		// current kprobe context is p
		if ((struct kprobe *)this_cpu_read(*temp) == p) {
			pr_info("%s: found on CPU %d at %p", MODNAME,
				smp_processor_id(), temp);
			this_cpu_write(saved_kprobe_context_p, temp);
			atomic_inc(&sys_thr_cxt->hack_ready_on_cpu);
			break;
		}
	}
	if (temp == 0) {
		pr_err("%s: NOT found on CPU %d", MODNAME, smp_processor_id());
		return 0;
	}

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
	search_probe.symbol_name = "dummy_run";
	search_probe.pre_handler = pre_handler_search;

	ret = register_kprobe(&search_probe);
	if (ret < 0) {
		pr_err("%s: register_kprobe failed, returned %d\n", MODNAME,
		       ret);
		return ret;
	}

	/* Execute "dummy_run" on each CPU to trigger the search probe
	 * pre-handler */
	on_each_cpu(dummy_run, NULL, 1);
	if (atomic_read(&sys_thr_cxt->hack_ready_on_cpu) < num_online_cpus()) {
		pr_err("%s: load_hack_search did not complete on every CPU.",
		       MODNAME);
	}

	/* Unregister the search probe */
	unregister_kprobe(&search_probe);

	pr_info("%s: load_hack_search completed.", MODNAME);

	return 0;
}