#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/atomic.h>
#include <linux/preempt.h>
#include <linux/irqflags.h>

#define MODNAME "SYSCALL-THROTTLE"

#define AUDIT if (1)

#define target_func "x64_sys_call"

static struct kprobe the_probe;
static atomic_t the_counter = ATOMIC_INIT(0);

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
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

int init_module(void)
{
    int ret;

    pr_info("%s: module correctly loaded\n", MODNAME);

    the_probe.symbol_name = target_func;
    the_probe.pre_handler = handler_pre;

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
