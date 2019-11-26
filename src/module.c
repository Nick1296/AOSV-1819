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
// dentry management
#include<linux/namei.h>
#include<linux/path.h>
#include<linux/dcache.h>
//error management
#include<linux/err.h>
#include <uapi/linux/limits.h>
#include <linux/uaccess.h>

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

/// \brief parameter that keeps the path to the directory in which session sematic is enabled
static char* sess_path;
module_param(sess_path,charp,0664);
MODULE_PARM_DESC(sess_path,"path in which session sematic is enabled");

/** \brief Check if the given path is a subpath of \ref sess_path
 *
 * Gets the dentry from the given path and from  \ref sess_path and check is the second dentry is an ancestor of the first dentry.
 * \todo implement!
 * \param[in] path Path to be checked
 * \returns 1 if the given path is a subpath of \ref sess_path and 0 otherwise; -1 is returned on error.
 */
int path_check(char* path){
	struct path psess,pgiven;
	struct dentry *dsess,*dgiven, *dentry;
	int retval;
	//get dentry from the sess_path
	retval=kern_path(sess_path,LOOKUP_FOLLOW,&psess);
	if(retval!=0){
		return retval;
	}
	dsess=psess.dentry;
	//get dentry from given path
	retval=kern_path(path,LOOKUP_FOLLOW,&pgiven);
	if(retval!=0){
		return retval;
	}
	dgiven=pgiven.dentry;
	
	dentry=dgiven;
	//check if dsess is an ancestor of dgiven
	while(!IS_ROOT(dentry)){
		if(dentry == dsess){
			return 1;
		} else{
			dentry=dentry->d_parent;
		}
	}
	return 0;
}

/**
 * \brief Used kprobes.
 *
 * Kprobes used to hook into the syscalls, they
 * allocated as an array.
 */
static struct kprobe* kps;

/**
 * \brief Used kretprobes.
 *
 * Kretprobes used to hook at the end of the VFS syscalls,
 * they are allocated as an array.
 */
//static struct kretprobe* krps;

///Number of used kprobes.
#define NKP 5
///Number of used kretprobes.
//#define NKRP 0


/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define SESSION_OPEN 00000004

/**
 * Simple test which outputs a message
 * every time the handler is fired.
 */
int test(struct kprobe *p, struct pt_regs *regs){
	int flag=0,path=0;
	char* path_str=kmalloc(PATH_MAX*sizeof(char),GFP_KERNEL);
	int ret=copy_from_user(path_str,(const void __user *)regs->cx,PATH_MAX*sizeof(char));
	flag=regs->dx & SESSION_OPEN;
	path=path_check(path_str);
	if(flag==4 && path==1){
		printk(KERN_INFO "session folder: %s\n",sess_path);
		printk(KERN_INFO "correctly hooked on a session open call\n");
		printk(KERN_INFO "given path: %s\n",path_str);
		printk(KERN_INFO "copy_from_user result: %d\n",PATH_MAX-ret);
		printk(KERN_INFO "check results: path=%d flag=%d\n",path,flag);
	}
	return 0;
}

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

	//session path initialization
	sess_path = kmalloc(PATH_MAX,GFP_KERNEL);
	strcpy(sess_path,"/home/nick1296/Workspace");
	
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
