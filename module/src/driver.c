#include "syscall-throttle.h"

static int dev_open(struct inode *, struct file *);

static struct file_operations fops = {.owner = THIS_MODULE, .open = dev_open};

static int dev_open(struct inode *inode, struct file *file) { return 0; }