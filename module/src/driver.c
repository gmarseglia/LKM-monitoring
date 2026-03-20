#include "syscall-ioctl.h"
#include "syscall-throttle.h"

#define MAX_STR_LEN 64

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static long dev_ioctl(struct file *, unsigned int, unsigned long);

static int create_device(void);
static void destroy_device(void);

static struct file_operations fops = {.owner = THIS_MODULE,
				      .open = dev_open,
				      .release = dev_release,
				      .unlocked_ioctl = dev_ioctl};

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

static int convert_to_string(unsigned long param, char *str_param)
{
	int str_len;
	str_len = strncpy_from_user(str_param, (char *)param, MAX_STR_LEN - 1);
	str_param[str_len] = '\0';

	return str_len;
}

static long
dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{
	pr_info("%s: ioctl invoked with command=%d and param=%lu", MODNAME,
		_IOC_NR(command), param);

	char str_param[64];

	switch (command) {
	case IOCTL_REGISTER_PID:
		// #TODO: check results better
		if (convert_to_string(param, str_param) > 0)
			register_critical(str_param,
					  &sys_thr_cxt->pids_registry);
		break;
	case IOCTL_UNREGISTER_PID:
		if (convert_to_string(param, str_param) > 0)
			unregister_critical(str_param,
					    &sys_thr_cxt->pids_registry);
		break;
	default:
		pr_warn("%s: Unknown ioctl command: %u\n", MODNAME, command);
		return -ENOTTY;
	}

	return 0;
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

	int ret = create_device();

	return ret;
}

void unload_driver(void)
{
	destroy_device();

	// #TODO: check if devices are still open
	unregister_chrdev(sys_thr_cxt->Major, MODNAME);
}

static int create_device(void)
{
	sys_thr_cxt->my_class = class_create(MODNAME);
	if (IS_ERR(sys_thr_cxt->my_class)) { // #TODO: what is this?
		unregister_chrdev(sys_thr_cxt->Major, MODNAME);
		return PTR_ERR(sys_thr_cxt->my_class);
	}

	sys_thr_cxt->my_device =
		device_create(sys_thr_cxt->my_class, NULL,
			      MKDEV(sys_thr_cxt->Major, 0), NULL, "sys_thr");
	if (IS_ERR(sys_thr_cxt->my_device)) {
		class_destroy(sys_thr_cxt->my_class);
		unregister_chrdev(sys_thr_cxt->Major, MODNAME);
		return PTR_ERR(sys_thr_cxt->my_device);
	}

	return 0;
}

static void destroy_device(void)
{
	device_destroy(sys_thr_cxt->my_class, MKDEV(sys_thr_cxt->Major, 0));
	class_destroy(sys_thr_cxt->my_class);
}