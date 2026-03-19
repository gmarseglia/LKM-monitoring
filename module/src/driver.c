#include "syscall-throttle.h"

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);

static struct file_operations fops = {.owner = THIS_MODULE,
				      .open = dev_open,
				      .read = dev_read,
				      .release = dev_release};

static int dev_open(struct inode *inode, struct file *file)
{
	pr_info("%s: dev_open request", MODNAME);
	return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
	pr_info("%s: dev_release request", MODNAME);
	return 0;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{
	char tmp[64];
	int tmp_len;
	int ret;

	tmp_len = snprintf(tmp, sizeof(tmp), "hello world");

	mutex_lock(&sys_thr_cxt->operation_synchronizer);

	ret = copy_to_user(buff, tmp, tmp_len);

	mutex_unlock(&sys_thr_cxt->operation_synchronizer);

	return len;
}

int load_driver(void)
{
	int Major = register_chrdev(0, MODNAME, &fops);
	if (Major < 0) {
		pr_err("%s: register_chrdev returned %d", MODNAME, Major);
		return Major;
	}
	pr_info("%s: char device driver registered with Major=%d", MODNAME,
		Major);
	sys_thr_cxt->Major = Major;
	return 0;
}

void unload_driver(void)
{
	// #TODO: check if devices are still open
	unregister_chrdev(sys_thr_cxt->Major, MODNAME);
}