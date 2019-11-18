#include <linux/kernel.h>
#include <linux/kprobes.h>

/*
 * Simple test which outputs a message
 * every time the handler is fired.
 */
int test(struct kprobe *p, struct pt_regs *regs){
	printk(KERN_INFO "kprobe hooked\n");
	return 0;
}
