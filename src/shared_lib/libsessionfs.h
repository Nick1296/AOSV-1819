/** \page sh_lib Shared Library
 * This library wraps the `open` and `close` systemcalls determine if this is a "normal" syscall, or must be redirected
 * to the SessionFS kernel module, because it uses the unix session semantics.
 */

/** \file
 * \brief Shared library header.
 * Header file for the shared library that wraps the `open` and `close` functions.
 */

/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define O_SESS 00000004

/** \brief Gets the session path.
 * \param[out] buf The buffer which will contain the output, must be provided.
 * \param[in] buflen The length of the provided buffer.
 * \return The number of bytes read or an error code.
 */
int get_sess_path(char * buf,int buflen);

/** \brief Changes the session path.
 * \param[in] buf The buffer which will contain the new path.
 * \param[in] buflen The length of the provided buffer.
 * \return The number of bytes written or an error code.
 */
int change_sess_path(char* path,int pathlen);