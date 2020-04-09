/** \file
 * \brief Module configuration, component of the _Module Configuration_ submodule.
 *
 * This file contains the module configuration and the functions that will be executed when the module is loaded and unloaded.
*/

#include <linux/kernel.h>
#include <linux/module.h>

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
MODULE_VERSION("0.5");

/// We set the session path as a rad-only module parameter.
module_param(sess_path,charp,0444);
MODULE_PARM_DESC(sess_path,"path in which session sematic is enabled");

/** \brief Loads the device when the kernel module is loaded in the kernel
 * \returns 0 on success, and error code on fail
 */
static int __init sessionFS_load(void){
	int ret;
	ret=init_device();
	printk(KERN_INFO "SessionFS: module loaded\n");
	return ret;
}

/**
 * \brief Before unloading the module we relase the device.
 */
static void __exit sessionFS_unload(void){
	printk(KERN_INFO "SessionFS: shutting down the device");
	release_device();
	printk(KERN_INFO "SessionFS: device powered off");
}
/// Specification of the module init function
module_init(sessionFS_load);
/// Specification of the module cleanup function
module_exit(sessionFS_unload);
