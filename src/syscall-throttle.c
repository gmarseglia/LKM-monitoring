#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Giuseppe Marseglia <g.marseglia.it@gmail.com>");
MODULE_DESCRIPTION("See README.md");

#define MODNAME "SYSCALL-THROTTLER"

int init_module(void)
{
    printk("%s: module correctly loaded\n", MODNAME);
    return 0;
}

void cleanup_module(void)
{
    printk("%s: module correctly unloaded\n", MODNAME);
}