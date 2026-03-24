#ifndef IOCTL_SHARED_H
#define IOCTL_SHARED_H

#include <linux/ioctl.h>

/* Define a "magic" character for our driver (should be unique) */
#define MY_MACRO_MAGIC 'k'

/* * Define the ioctl commands:
 * _IOW: Write data to the kernel (User -> Kernel)
 * _IOR: Read data from the kernel (Kernel -> User)
 */
#define IOCTL_REGISTER_NR _IOW(MY_MACRO_MAGIC, 0, int)
#define IOCTL_UNREGISTER_NR _IOW(MY_MACRO_MAGIC, 1, int)
#define IOCTL_REGISTER_PID _IOW(MY_MACRO_MAGIC, 2, char *)
#define IOCTL_UNREGISTER_PID _IOW(MY_MACRO_MAGIC, 3, char *)

#endif