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
// dentry management
#include<linux/namei.h>
#include<linux/path.h>
#include<linux/dcache.h>
//error management
#include<linux/err.h>
#include <uapi/linux/limits.h>
#include <linux/uaccess.h>

//our custom virtual device
#include "device_sessionfs_mod.h"

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
MODULE_VERSION("0.02");


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

/** \brief load the kernel module and the device
 * \returns 0 on success, and error code on fail
 */
static int __init sessionFS_load(void){
	int ret;
	ret=init_device();
	printk(KERN_INFO "module loaded\n");
	return ret;
}

/**
 * Before unloading the module we need to close every opened session and remove the kprobes that we inserted during the
 * initialization.
 * \todo free and unregister all used kprobes
 * \todo force close every opened session
 */
static void __exit sessionFS_unload(void){
	release_device();
		printk(KERN_INFO "module unloaded\n");
}
/// Specification of the module init function
module_init(sessionFS_load);
/// Specification of the module cleanup function
module_exit(sessionFS_unload);
