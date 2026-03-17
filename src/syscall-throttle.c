#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/atomic.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>
#include <linux/smp.h>

#define MODNAME "SYSCALL-THROTTLE"

#define AUDIT if (1)

#define target_func "x64_sys_call"

static struct kprobe the_probe;
static atomic_t the_counter = ATOMIC_INIT(0);

DEFINE_PER_CPU(struct kprobe *, kprobe_context_position);

static int __kprobes pre_handler_throttle(struct kprobe *p, struct pt_regs *regs)
{
    // #TODO: why orig_ax works?
    struct pt_regs *the_regs = (struct pt_regs *)regs->di;
    unsigned long sys_call_number = the_regs->orig_ax;

    if (preempt_count() == 0)
    {
        pr_alert("%s: found preemptable, aborting.", MODNAME);
        return 0;
    }

    if (irqs_disabled())
    {
        pr_alert("%s: irqs are disabled, aborting.", MODNAME);
        return 0;
    }

    if (current->pid == 14313)
    // if (sys_call_number == 0 && current->pid == 9140)
    {
        int counted;
        counted = atomic_inc_return(&the_counter);

        printk_ratelimited(
            KERN_INFO "%s: probe hit %d times, last for pid %d, with ax=%lu",
            MODNAME, counted, current->pid, sys_call_number);
    }

    /* A pre_handler must return 0 unless it handles the fault itself */
    return 0;
}

void dummy_run(void *arg)
{
    pr_info("%s: dummy running on CPU %d", MODNAME, smp_processor_id());
    return;
}

static int __kprobes pre_handler_search(struct kprobe *p, struct pt_regs *regs)
{
    struct kprobe **temp = (struct kprobe **)&kprobe_context_position;

    while (temp > 0)
    {
        temp--;
        if ((unsigned long)__this_cpu_read(*temp) == (unsigned long)p)
        {
            printk("%s: found on CPU %d at %p", MODNAME, smp_processor_id(), temp);
            break;
        }
    }

    if (temp == 0)
    {
        pr_err("%s: NOT found on CPU %d", MODNAME, smp_processor_id());
        return 0;
    }

    // struct kprobe **re_read = __this_cpu_read(*kprobe_context_position);

    pr_info("%s: pre_handler_search running on CPU %d", MODNAME, smp_processor_id());
    return 0;
}

int load_hack(void)
{
    int ret_search;
    struct kprobe search_probe;

    search_probe.symbol_name = "dummy_run";
    search_probe.pre_handler = pre_handler_search;

    ret_search = register_kprobe(&search_probe);
    if (ret_search < 0)
    {
        pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret_search);
        return ret_search;
    }

    // smp_call_function(dummy_run, NULL, 1);
    on_each_cpu(dummy_run, NULL, 1);

    pr_info("%s: load_hack completed.", MODNAME);

    unregister_kprobe(&search_probe);

    return 0;
}

int init_module(void)
{
    int ret;

    pr_info("%s: module correctly loaded\n", MODNAME);

    load_hack();

    the_probe.symbol_name = target_func;
    the_probe.pre_handler = pre_handler_throttle;

    ret = register_kprobe(&the_probe);
    if (ret < 0)
    {
        pr_err("%s: register_kprobe failed, returned %d\n", MODNAME, ret);
        return ret;
    }

    return 0;
}

void cleanup_module(void)
{
    unregister_kprobe(&the_probe);
    pr_info("%s: kprobe at %p unregistered\n", MODNAME, the_probe.addr);

    pr_info("%s: module correctly unloaded\n", MODNAME);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");
