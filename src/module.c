/** \mainpage
 * \section intro Introduction
 * SessionFS is a virtual file system wrapper which allows an userspace application to use the Unix session semantics to
 * operate on files.
 * The module allows the semantic to be used inside a folder which can be specified or changed by the userspace
 * application anytime (defaults to `/mnt`).
 *
 * \section specs Module specification
 * The module is compiled for the latest `linux-lts` kernel (which now is 4.19) and can be loaded and unloaded freely.
 * Wrapping of the FS syscalls is done via kprobes, to allow the userspace application to enable the usage of a session
 * semantic with the custom defined `O_SESS` flag when opening a file.
 */

/** \file
 * \brief Module configuration.
 *
 * This file contains the module configuration and the functions that will be executed when the module is loaded and unloaded.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
//error codes
#include <uapi/asm-generic/errno-base.h>
//kprobes intefaces
#include<linux/kprobes.h>
//kmalloc etc..
#include<linux/slab.h>

#include "hooks.h"
/**
 * \brief Specification of the license used by the module.
 * Close sourced module cannot access to all the kernel facilities.
 * This is intended to avoid open source code to be stolen by closed source developers.
 */
MODULE_LICENSE("GPL");
/// Module author specification.
MODULE_AUTHOR("Mattia Nicolella <mattianicolella@gmail.com>");
/// A short description of the module.
MODULE_DESCRIPTION("A session based virtual filesystem wrapper");
/// Module version specification
MODULE_VERSION("0.01");

/**
 * The module initialization is done by inserting a kprobe in each of the following functions:
 * - open
 * - release
 * - read
 * - write
 * - llseek
 *
 * This allows the wrapper to implement the session semantics without modifying with the systemcall table or the syscalls
 * themselves.
 * \returns 0 on successful initialization or a code in `uapi/asm-generic/errno-base.h` that specifies the error.
 *
 * \todo insert krprobes in all the interested syscalls (now only open has a kprobe).
 */
static int __init sessionFS_load(void){
	int ret=0;							// return value
	struct kprobe* kp;			//used to move into the kps array
	//struct kretprobe* krp; 	//used to move into the krps array

	printk(KERN_INFO "initializing kprobes\n");
	//allocating the kprobe structures
	kps=(struct kprobe*) kzalloc(sizeof(struct kprobe)*NKP, GFP_KERNEL);

	//initializing open kprobe
	kp=kps;
	kp->pre_handler=test;
	kp->symbol_name="do_sys_open";
	ret=register_kprobe(kp);
	if(ret==0){
		printk(KERN_DEBUG "succesfully registered kprobe on do_sys_open\n");
	}else{
		printk(KERN_ERR "registration of kprobe in do_sys_open failed\n");
		return -EPERM;
	}


	printk(KERN_INFO "loaded kprobes\n");
	return 0;
}

/**
 * Before unloading the module we need to close every opened session and remove the kprobes that we inserted during the
 * initialization.
 * \todo free and unregister all used kprobes 
 * \todo force close every opened session
 */
static void __exit sessionFS_unload(void){
	//unregistering kprobes
	unregister_kprobe(kps);
	kfree(kps);
	printk(KERN_INFO "kprobes released\n");
}
/// Specification of the module init function
module_init(sessionFS_load);
/// Specification of the module cleanup function
module_exit(sessionFS_unload);
