/** \page sh_lib Shared Library
 * This library wraps the `open` and `close` systemcalls determine if this is a "normal" syscall, or must be redirected
 * to the SessionFS kernel module, because it uses the unix session semantics.
 */

/// used to enable RTLD_NEXT
#define _GNU_SOURCE
/// used to load the "original" `open` and `close`
#include <dlfcn.h>

/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define SESSION_OPEN 00000004

///we use a typedef to alias the function pointer to the libc `open`
typedef int (*orig_open)(const chat pathname, int flags);

/// we use a typedef to alis the function pointer to the libc `close`
typedef int (*orig_close)(int filedes);

///a program constructor which saves the original value for the `open` and `close` symbols
static __attribute__((constructor)) void init_method(){

}
