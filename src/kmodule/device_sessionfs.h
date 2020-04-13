/** \file
 * \brief Properties of the devices necessary for the device implementation and the library that uses the device, component of the _Character Device_ submodule.
 *
 * This file contains some general device properties, such as the available ioctls and the device name that are needed to
 * the file that will implement the device and to the library the will use the ioctls.
 */

#ifndef DEV_H
#define DEV_H

#include <linux/ioctl.h>
#include <linux/types.h>

/** A major device number is necessary to identify our virtual device, since it doensn't have an assigned letter.
 * We use 120 as major number since it reserved for local and experimental use. See: Documentation/admin-guide/devices.txt
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

/// The ioctl sequence number that idenfies the request for the device shutdown.
#define IOCTL_SEQ_SHUTDOWN 10

/** \brief Flag used to enable the Unix session semantic.
 *
 *  Unused flag in `include/uapi/asm-generic/fcntl.h`, that has been repurposed.
 */
#define O_SESS 10000000


///Defines the validity of a session
#define VALID_SESS 0

/**
 * \struct sess_params
 * \param orig_path The pathname of the original file to be opened in a session, or that represents the original file containing the incarnation to be closed.
 * \param flags The flags used to determine the incarnation permissions.
 * \param mode The permissions to apply to newly created files.
 * \param pid The pid of the process that requests the creation of an incarnation.
 * \param filedes The file descriptor of the incarnation.
 * \param valid The session can be invalid if there was an error in the copying of the original file over the incarnation file, so the value of this parameter can be <= `::VALID_SESS`.
 *
 * This struct will hold all the necessary parameters used to open and close sessions.
*/
struct sess_params{
	const char* orig_path;
	int flags;
	mode_t mode;
	pid_t pid;
	int filedes;
	int valid;
};

/** \brief We define the ioctl command for opening a session.
 *
 * We use the `_IOWR` macro since we need to pass to the virtual device the `::sess_params` struct.
 */
#define IOCTL_OPEN_SESSION _IOWR(MAJOR_NUM,IOCTL_SEQ_OPEN,struct sess_params*)

/** \brief We define the ioctl command for closing a session.
 *
 * We use the macro `_IOWR` since we need to pass to the virtual device the `sess_params` struct.
 */
#define IOCTL_CLOSE_SESSION _IOWR(MAJOR_NUM,IOCTL_SEQ_CLOSE,struct sess_params*)

/** \brief We define the ioctl command fot asking a device shutdown
 *
 * We use the `_IOR` macro since the device will let the userspace program read the number of active sessions during shutdown.
 */
#define IOCTL_DEVICE_SHUTDOWN _IOR(MAJOR_NUM,IOCTL_SEQ_SHUTDOWN,int*)

#endif
