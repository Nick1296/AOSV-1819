#ifndef HOOKS
/** \file
 * \brief Kprobe handlers.
 *
 * Implementation of all the handlers used by the registered kprobes.
 */

/**
 * \brief Used kprobes.
 *
 * Kprobes used to hook into the syscalls, they
 * allocated as an array.
 */
struct kprobe* kps;

/**
 * \brief Used kretprobes.
 *
 * Kretprobes used to hook at the end of the VFS syscalls,
 * they are allocated as an array.
 */
struct kretprobe* krps;

///Number of used kprobes.
#define NKP 5
///Number of used kretprobes.
#define NKRP 0


/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define SESSION_OPEN 00000004

/// kprobe insertion test
int test(struct kprobe *p, struct pt_regs *regs);


#endif /* HOOKS */
