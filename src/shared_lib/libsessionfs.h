/** \page sh_lib Shared Library
 * This library wraps the `open` and `close` systemcalls determine if this is a "normal" syscall, or must be redirected
 * to the SessionFS kernel module, because it uses the unix session semantics.
 */

/** \file 
 * \brief Shared library header.
 * Header file for the shared library that wraps the `open` and `close` functions.
 */ 

/// Enables RTLD_NEXT macro.
#define _GNU_SOURCE
// dlsym function
#include <dlfcn.h>

/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define O_SESS 00000004

///we use a typedef to alias the function pointer to the libc `open`.
typedef int (*orig_open_type)(const char* pathname, int flags);

/// we use a typedef to alis the function pointer to the libc `close`.
typedef int (*orig_close_type)(int filedes);

/**
 * \brief Wraps the open determining if it must call the libc `open` or the SessionFS module.
 * \param[in] pathname The pathname of the file to be opened, same usage an type of the libc `open`'s `pathname`.
 * \param[in] flags flags to determine the file status flag and the access modes, same as the libc `open`'s `oflag`, however a possible flag is the ::O_SESS flag which enables the session semantic.
 * \returns It will return a file descriptor if the operation is successful, both for the libc version and for the module return value. 
 */
int open(const char* pathname, int flags);

/**
 * \brief Wraps the close determining if it must call the libc `close` or the SessionFS module.
 * \param[in] filedes file descriptor to deallocate, same as libc `open`'s `fildes`.
 * \returns 0 on success, -1 on error, setting errno to indicate the error value.
 */
int close(int filedes);
