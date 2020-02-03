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

/** \brief Flag that enables the session semantic.
 *  Unused flag in `include/uapi/asm-generic/fcntl.h` that is repurposed to be used to enable the session semantic.
 */
#define SESSION_OPEN 00000004

/**
 * \brief The module initialization is done by defining a folder in which sessions are enabled and by initializing the needed data structures.
 *
 * \returns 0 on successful initialization or a code in `uapi/asm-generic/errno-base.h` that specifies the error.
 *
 * \todo initialize data structures
 */
static int __init sessionFS_load(void){
	int ret=0;							// return value

	//session path initialization
	sess_path = kmalloc(PATH_MAX,GFP_KERNEL);
	strcpy(sess_path,"/home/nick1296/Workspace");

	printk(KERN_INFO "loaded module\n");
	return 0;
}

/**
 * Before unloading the module we need to close every opened session and free al the uses data structures.
 * \todo free all data structures
 * \todo force close every opened session
 */
static void __exit sessionFS_unload(void){
	printk(KERN_INFO "module unloaded\n");
}
/// Specification of the module init function
module_init(sessionFS_load);
/// Specification of the module cleanup function
module_exit(sessionFS_unload);
