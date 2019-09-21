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

/// \brief Kprobe insertion test.
/**
 *  @param p kprobe which triggers the handler.
 *  @param regs CPU status snapshot.
 *  \returns 0
 */
int test(struct kprobe *p, struct pt_regs *regs);

#endif /* HOOKS */
