#ifndef IOCTL_SHARED_H
#define IOCTL_SHARED_H

#include <linux/ioctl.h>

/* Define a "magic" character for our driver (should be unique) */
#define MY_MACRO_MAGIC 'k'

/* * Define the ioctl commands:
 * _IOW: Write data to the kernel (User -> Kernel)
 * _IOR: Read data from the kernel (Kernel -> User)
 */

#define IOCTL_START_THROTTLE        _IO(MY_MACRO_MAGIC, 0)
#define IOCTL_STOP_THROTTLE         _IO(MY_MACRO_MAGIC, 1)
#define IOCTL_REGISTER_NR           _IOW(MY_MACRO_MAGIC, 2, int)
#define IOCTL_UNREGISTER_NR         _IOW(MY_MACRO_MAGIC, 3, int)
#define IOCTL_REGISTER_EUID         _IOW(MY_MACRO_MAGIC, 4, char *)
#define IOCTL_UNREGISTER_EUID       _IOW(MY_MACRO_MAGIC, 5, char *)
#define IOCTL_REGISTER_PROG_NAME    _IOW(MY_MACRO_MAGIC, 6, char *)
#define IOCTL_UNREGISTER_PROG_NAME  _IOW(MY_MACRO_MAGIC, 7, char *)

#endif