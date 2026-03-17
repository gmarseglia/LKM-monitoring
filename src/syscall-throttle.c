#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#define MODNAME "SYSCALL-THROTTLER"

#define AUDIT if (1)

#define target_func "x64_sys_call"

static struct kprobe the_probe;

static int __kprobes handler_pre(struct kprobe *p, struct pt_regs *regs)
{
    /* * Using printk_ratelimited prevents system freezes from log flooding
     * when hooking highly active functions.
     */
    printk_ratelimited(KERN_INFO "kprobe: hit %s at %p\n", p->symbol_name, p->addr);

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