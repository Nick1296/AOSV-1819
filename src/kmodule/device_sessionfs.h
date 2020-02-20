/** \file
 * \brief Properties of the devices necessary for the device implementation and the library that uses the device.
 * This file contains some general device properties, such as the available ioctls and the device name that are needed to
 * the file that will implement the device and to the library the will use the ioctls.
 */

#ifndef DEV_H
#define DEV_H

#include <uapi/asm-generic/ioctl.h>
#include <linux/types.h>

/** A major device number is necessary to identify our virtual device, since it doensn't have an assigned letter.
 * We use 120 as major number since it reserved for local and experimental use. <a href="Documentation/admin-guide/devices.txt">Source</a>
 */
#define MAJOR_NUM 120

///The name of our virtual device.
#define DEVICE_NAME "SessionFS_dev"

///The name of the corresponding device class
#define CLASS_NAME "SessionFS_class"

/// The ioctl sequence number that indenfies the opening of a session.
#define IOCTL_SEQ_OPEN 0

/// The ioctl sequence number that idenfies the closing of a session.
#define IOCTL_SEQ_CLOSE 1

///The `O_SESS` flag, which will enable session semantic if used with a compliant path
#define O_SESS 00000004

/**
 * \struct sess_params
 * \param orig_path The pathname of the original file to be opened in a session
 * We define a struct that will hold the pathanme and flags that determine the behaviour of the session opening
*/
struct sess_params{
	const char* orig_path;
	int flags;
	pid_t pid;
	const char* inc_path;
	int filedes;
};

/** We define the ioctl command for opening a session.
 * We use the ::_IOW macro since we need to pass to the virtual device the pathname and the flags that control the session.
 */
#define IOCTL_OPEN_SESSION _IOW(MAJOR_NUM,IOCTL_SEQ_OPEN,struct open_params*)

/** We define the ioctl command for closing a session.
 * We use the macro ::_IOW since we need to pass to the file descriptor of the file we want to commit to the virtual device.
 */
#define IOCTL_CLOSE_SESSION _IOW(MAJOR_NUM,IOCTL_CLOSE_SESSION,int)

#endif
