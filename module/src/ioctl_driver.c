#include "syscall-ioctl.h"
#include "syscall-throttle.h"

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
	__ST_LOG_FINEST pr_info("%s: dev_open request", __ST_MODNAME);
	return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
	__ST_LOG_FINEST pr_info("%s: dev_release request", __ST_MODNAME);
	return 0;
}

static int string_from_user(unsigned long param, char *str_param)
{
	int str_len;
	str_len = strncpy_from_user(str_param, (char *)param,
				    __ST_MAX_STR_LEN - 1);
	if (str_len < 0)
		return str_len;
	str_param[str_len] = '\0';
	return str_len;
}

static long
dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{
	char str_param[__ST_MAX_STR_LEN];
	int int_param;

	__ST_LOG_FINE pr_info("%s: ioctl invoked with command=%d and param=%lu",
			      __ST_MODNAME, _IOC_NR(command), param);

	/* Convert param according to command */
	switch (command) {
	case IOCTL_START_THROTTLE:
	case IOCTL_STOP_THROTTLE:
		// No params for these commands
		break;
	case IOCTL_REGISTER_NR:
	case IOCTL_UNREGISTER_NR:
	case IOCTL_SET_LIMIT:
		int_param = (int)param;
		__ST_LOG_FINEST pr_info("%s: ioctl executing with int_param=%d",
					__ST_MODNAME, int_param);
		break;
	case IOCTL_REGISTER_EUID:
	case IOCTL_UNREGISTER_EUID:
	case IOCTL_REGISTER_PROG_NAME:
	case IOCTL_UNREGISTER_PROG_NAME:
		if (string_from_user(param, str_param) < 0) {
			pr_warn("%s: Unable to convert param to string\n",
				__ST_MODNAME);
			return -EINVAL;
		}
		__ST_LOG_FINE pr_info("%s: ioctl executing with str_param=%s",
				      __ST_MODNAME, str_param);
		break;
	default:
		pr_warn("%s: Unknown ioctl command: %u\n", __ST_MODNAME,
			command);
		return -ENOTTY;
	}

	/* Execute command */
	int ret = 0;
	switch (command) {
	case IOCTL_START_THROTTLE:
		atomic_set(&st_cxt->throttle_running, 1);
		break;
	case IOCTL_STOP_THROTTLE:
		atomic_set(&st_cxt->throttle_running, 0);
		update_limit_and_wake();
		break;
	case IOCTL_REGISTER_NR:
		ret = register_critical_num(int_param);
		break;
	case IOCTL_UNREGISTER_NR:
		ret = unregister_critical_num(int_param);
		break;
	case IOCTL_REGISTER_EUID:
		mutex_lock(&st_cxt->ioctl_mutex);
		ret = register_critical_str(str_param, &st_cxt->euid_registry);
		mutex_unlock(&st_cxt->ioctl_mutex);
		break;
	case IOCTL_UNREGISTER_EUID:
		mutex_lock(&st_cxt->ioctl_mutex);
		ret = unregister_critical_str(str_param,
					      &st_cxt->euid_registry);
		mutex_unlock(&st_cxt->ioctl_mutex);
		break;
	case IOCTL_REGISTER_PROG_NAME:
		mutex_lock(&st_cxt->ioctl_mutex);
		ret = register_critical_str(str_param,
					    &st_cxt->prog_names_registry);
		mutex_unlock(&st_cxt->ioctl_mutex);
		break;
	case IOCTL_UNREGISTER_PROG_NAME:
		mutex_lock(&st_cxt->ioctl_mutex);
		ret = unregister_critical_str(str_param,
					      &st_cxt->prog_names_registry);
		mutex_unlock(&st_cxt->ioctl_mutex);
		break;
	case IOCTL_SET_LIMIT:
		atomic_set(&st_cxt->crit_limit, int_param);
		ret = 0;
		break;
	default:
		pr_warn("%s: Unknown ioctl command: %u\n", __ST_MODNAME,
			command);
		ret = -ENOTTY;
	}

	return ret;
}

int load_driver(void)
{
	int Major;
	int ret;

	Major = register_chrdev(0, __ST_MODNAME, &fops);
	if (Major < 0) {
		pr_err("%s: register_chrdev returned %d", __ST_MODNAME, Major);
		return Major;
	}
	pr_info("%s: char device driver registered with Major=%d", __ST_MODNAME,
		Major);
	st_cxt->Major = Major;

	ret = create_device();

	return ret;
}

void unload_driver(void)
{
	destroy_device();

	// #TODO: check if devices are still open
	unregister_chrdev(st_cxt->Major, __ST_MODNAME);
}

static int create_device(void)
{
	st_cxt->my_class = class_create(__ST_MODNAME);
	if (IS_ERR(st_cxt->my_class)) { // #TODO: what is this?
		unregister_chrdev(st_cxt->Major, __ST_MODNAME);
		return PTR_ERR(st_cxt->my_class);
	}

	st_cxt->my_device =
		device_create(st_cxt->my_class, NULL, MKDEV(st_cxt->Major, 0),
			      NULL, "sys_thr");
	if (IS_ERR(st_cxt->my_device)) {
		class_destroy(st_cxt->my_class);
		unregister_chrdev(st_cxt->Major, __ST_MODNAME);
		return PTR_ERR(st_cxt->my_device);
	}

	return 0;
}

static void destroy_device(void)
{
	device_destroy(st_cxt->my_class, MKDEV(st_cxt->Major, 0));
	class_destroy(st_cxt->my_class);
}