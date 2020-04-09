/** \file libsessionfs.h
 * \brief Shared library header.
 *
 * Header file for the shared library that wraps the `open` and `close` functions.
 * Contains only ::get_sess_path and ::write_sess_path since the open() and close() functions are used to wrap the libc
 * syscalls and do not need to be exported.
 */

//to enable PATH_MAX
#include <errno.h>
#include <limits.h>

///For the ioctls methods exposed by our char device
#include "../kmodule/device_sessionfs.h"

/** \brief Gets the current session path.
 * \param[out] buf The buffer which will contain the output, must be provided.
 * \param[in] buflen The length of the provided buffer.
 * \return The number of bytes read or an error code.
 */
int get_sess_path(char * buf,int buflen);

/** \brief Changes the current session path.
 * \param[in] buf The buffer which will contain the new path.
 * \param[in] buflen The length of the provided buffer.
 * \return The number of bytes written or an error code.
 */
int write_sess_path(char* path);

/** \brief Asks to shut down the `SessionFS_dev` device.
 * \return 0 on success, `-EAGAIN` if the device is in use and cannot be removed.
 */
int device_shutdown(void);
