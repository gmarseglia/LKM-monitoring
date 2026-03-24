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
	int nr;

	/* Convert param according to command */
	switch (command) {
	case IOCTL_START_THROTTLE:
	case IOCTL_STOP_THROTTLE:
		// No params for these commands
		break;
	case IOCTL_REGISTER_NR:
	case IOCTL_UNREGISTER_NR:
		nr = (int)param;
		pr_info("%s: ioctl executing with nr=%d", MODNAME, nr);
		break;
	case IOCTL_REGISTER_PID:
	case IOCTL_UNREGISTER_PID:
	case IOCTL_REGISTER_EUID:
	case IOCTL_UNREGISTER_EUID:
	case IOCTL_REGISTER_PROG_NAME:
	case IOCTL_UNREGISTER_PROG_NAME:
		if (convert_to_string(param, str_param) < 0) {
			pr_warn("%s: Unable to convert param to string\n",
				MODNAME);
			return -EINVAL;
		}
		pr_info("%s: ioctl executing with str_param=%s", MODNAME,
			str_param);
		break;
	default:
		pr_warn("%s: Unknown ioctl command: %u\n", MODNAME, command);
		return -ENOTTY;
	}

	/* Execute command */
	int ret = 0;
	switch (command) {
	case IOCTL_START_THROTTLE:
		atomic_set(&sys_thr_cxt->running, true);
		break;
	case IOCTL_STOP_THROTTLE:
		atomic_set(&sys_thr_cxt->running, false);
		update_limit_and_wake();
		break;
	case IOCTL_REGISTER_NR:
		ret = register_critical_num(nr);
		break;
	case IOCTL_UNREGISTER_NR:
		ret = unregister_critical_num(nr);
		break;
	case IOCTL_REGISTER_PID:
		pr_info("%s: command=IOCTL_REGISTER_PID", MODNAME);
		ret = register_critical_str(str_param,
					    &sys_thr_cxt->pids_registry);
		break;
	case IOCTL_UNREGISTER_PID:
		ret = unregister_critical_str(str_param,
					      &sys_thr_cxt->pids_registry);
		break;
	case IOCTL_REGISTER_EUID:
		ret = register_critical_str(str_param,
					    &sys_thr_cxt->euid_registry);
		break;
	case IOCTL_UNREGISTER_EUID:
		ret = unregister_critical_str(str_param,
					      &sys_thr_cxt->euid_registry);
		break;
	case IOCTL_REGISTER_PROG_NAME:
		ret = register_critical_str(str_param,
					    &sys_thr_cxt->prog_names_registry);
		break;
	case IOCTL_UNREGISTER_PROG_NAME:
		ret = unregister_critical_str(
			str_param, &sys_thr_cxt->prog_names_registry);
		break;
	default:
		pr_warn("%s: Unknown ioctl command: %u\n", MODNAME, command);
		ret = -ENOTTY;
	}

	return ret;
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