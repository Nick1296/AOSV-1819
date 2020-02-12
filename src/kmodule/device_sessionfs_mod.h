/** \file
 * \brief Device properties needed to the kernel module.
 * This file contains the properties of the device that are needed to the kernel module, such as the prototypes of the
 * device init and cleanup functions and the buffer that will contain the path in which sessions are enabled.
 */

#ifndef DEV_MODULE
#define DEV_MODULE

/// Keeps the path to the directory in which session sematic is enabled (located in ::device_sessionfs.c).
extern char* sess_path;

/// functions to be called when a process interacts with our device (located in ::device_sessionfs.c).
extern struct file_operations* dev_ops;

/** \brief Device initialization and registration.
 * \returns 0 on success -1 on error.
 */
int init_device(void);

/** \brief Releases the device and frees the used memory.
 */
void release_device(void);
#endif
