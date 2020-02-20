/** \file
 * \brief Implementation of the virtual character device that will handle sessions.
 *
 * This file contains the implementation of the device operations that will handle the session semantics and the interaction
 * with the path where sessions are enabled.
 */

///Global device properties.
#include "device_sessionfs.h"
///Device properties for the kernel module.
#include "device_sessionfs_mod.h"
///Our session manager module
#include "session_manager.h"

// for file_operations struct, register_chrdev unregister_chrdev
#include <linux/fs.h>
//for kmalloc
#include<linux/slab.h>
// for copy_to_user and copy_from_user
#include<linux/uaccess.h>
// for PATH_MAX
#include<uapi/linux/limits.h>
//for class_create, class_destroy, device_created, device_destroy
#include<linux/device.h>
//for THIS_MODULE
#include<linux/module.h>
//for error numbers
#include <uapi/asm-generic/errno.h>

///The default session path when the device is initialized
#define DEFAULT_SESS_PATH "/mnt"

/// \brief parameter that keeps the path to the directory in which session sematic is enabled
///\todo check if this varaible must be protected from concurrent access
char* sess_path=NULL;

/// length of the of the path string
///\todo check if this variable must be protected from concurrent access
int path_len=0;

///File operations allowed on our device
struct file_operations* dev_ops=NULL;

///Device class
static struct class* dev_class=NULL;

///Device object
static struct device* dev=NULL;


/** \brief Get the path in which sessions are enabled.
 * \param[out] buffer The buffer in which the path is copied.
 * \param[in] buflen The lenght of the supplied buffer.
 * \param file unused, but necessary to fit the function into struct file_operations.
 * \param offset unused, but necessary to fit the function into struct file_operations.
 * \returns The number of bytes written in ::buffer, or an error code (EINVAL if one of the supplied parameters are invalid
 * or EAGAIN if the copy_to_user failed).
 * The device_read will copy the buffer that contains the path in which session sematics is enabled in the suplied buffer.
 */
static ssize_t device_read(struct file* file, char* buffer,size_t buflen,loff_t* offset){
	int bytes_not_read=0;
#ifdef DEBUG
	printk(KERN_DEBUG, "reading session path\n");
#endif
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen<path_len){
		return -EINVAL;
	}
	bytes_not_read=copy_to_user(buffer,sess_path,path_len);
	if(bytes_not_read>0){
		return -EAGAIN;
	}
	return path_len-bytes_not_read;
}

/** \brief Writes a new path in which sessions must be enabled.
 * \param[in] buffer The new path in which session are enabled.
 * \param[in] buflen The length of the provided buffer.
 * \param file unused, but necessary to fit the function into struct file_operations.
 * \param offset unused, but necessary to fit the function into struct file_operations.
 * \returns The number of bytes written in ::buffer, or an error code (EINVAL if one of the supplied parameters are invalid
 * or EAGAIN if the copy_from_user failed).
 * The device_write will overwrite the current path in which sessions are enbaled, without affecting existing sessions.
 */
static ssize_t device_write(struct file* file,const char* buffer,size_t buflen,loff_t* offset){
	int bytes_not_written=0;
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen>path_len){
		return -EINVAL;
	}
	bytes_not_written=copy_from_user(sess_path,buffer,buflen);

	if(bytes_not_written>0){
		return -EAGAIN;
	}
	return buflen-bytes_not_written;
}

/** \brief Allows every user to read and write the device file of our virtual device
 * With this callback we determine the permission of the inoed in /dev that represents our device, allowing every user to
 * read and write in it.
 */
static char *sessionfs_devnode(struct device *dev, umode_t *mode)
{
	if (!mode){
		return NULL;
	}
	if (dev->devt == MKDEV(MAJOR_NUM,0)){
		*mode =0666;
	}
	return NULL;
}

/** \brief Handles the ioctls calls from the shared library.
 * \param[in] file
 * \param[in] num The ioctl sequence number, used to identify the operation to be
 * executed, its possible values are ::IOCTL_SEQ_OPEN and ::IOCTL_SEQ_CLOSE.
 *\param[in] param The ioctl param, which is a ::struct 
 */
int device_ioctl(struct file * file, unsigned int num, unsigned long param){
	const char* orig_pathname=NULL;
	int res;
	struct sess_params* p;
	p=kzalloc(sizeof(struct sess_params), GFP_KERNEL);
	if(!p){
		return -ENOMEM;
	}
	//get the parameters struct from userspace
	res=copy_from_user(p,param,sizeof(struct sess_params));
	if(res>0){
		kfree(p);
		return -EINVAL
	}
	pathname=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!pathname){
		kfree(p);
		return -ENOMEM;
	}
	//we try to initialize the sess_params::inc_pathname with the content a sequence of 0, to see if if a valid userspace memory address
	res=copy_to_user(p->inc_pathname,pathname,sizeof(char)*PATH_MAX);
	if(res>0){
		kfree(pathname);
		kfree(p);
		return -EINVAL
	}
	switch(num){
		case IOCTL_SEQ_OPEN:
			//copy the pathname string to kernel space
			res=copy_from_user(pathname,p->orig_path,sizeof(char)*PATH_MAX);
			if(res>0){
				kfree(p);
				kfree(pathname);
				return -EINVAL;
			}
			//we create a new session incarnation
			struct incarnation* inc=NULL;
			inc=create_session(pathname,p->flags,p->pid);
			//return the erro if we have failed in creatig the session
			if(IS_ERR(inc)){
				kfree(p);
				kfree(pathname);
				return PTR_ERR(inc);
			}
			//now we must check that the created session is valid
			if(inc->status<0){
				//we copy the incarnation pathname into the corresponding parameter in the sess_struct.
				res=copy_to_user(p->inc_pathname,inc->pathname,sizeof(char)*PATH_MAX);
				if(res>0){
					//this should not happen since we have alredy tried to copy into this struct at the beginning.
					kfree(pathname);
					kfree(p);
					return -EINVAL
				}
			}
			//we set the file descriptor into the sess_struct.
			p->filedes=inc->filedes;
			//we overwrite the existing sess_struct in userspace
			res=copy_to_user(param,p,sizeof(struct sess_params));
			kfree(pathname);
			kfree(p);
			if(res>0){
				return -EAGAIN;
			}
			return inc->status;
			break;
	}
}

/** Initializes the devices by setting sess_path, path_len and dev_ops variables, then registers the devices.
 */
int init_device(void){
	int res;
	// allocate the path buffer and path_len
	sess_path=kzalloc(PATH_MAX*sizeof(char),GFP_KERNEL);
	strcpy(sess_path,DEFAULT_SESS_PATH);
	path_len=strlen(DEFAULT_SESS_PATH);
	//allocate and initialize the dev_ops struct
	dev_ops= kzalloc(sizeof(struct file_operations),GFP_KERNEL);
	dev_ops->read=device_read;
	dev_ops->write=device_write;
	/// \todo implement ioctls
	//dev_ops->unlocked_ioctl=ioctl;
	/// \todo allocate session info structures
	//init the session manager
	init_manager();
	//register the device
	res=register_chrdev(MAJOR_NUM,DEVICE_NAME,dev_ops);
	if(res<0){
		release_manager();
		printk(KERN_ALERT "failed to register the sessionfs virtual device\n");
		return res;
	}
	printk(KERN_INFO "Device %s registered\n", DEVICE_NAME);
	//register the device class
	dev_class=class_create(THIS_MODULE,CLASS_NAME);
	if (IS_ERR(dev_class)){
		release_manager();
		unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(dev_class);
	}
	//setting devnode
	dev_class->devnode=sessionfs_devnode;
	printk("SessionFS device class registered successfully\n");
	//register the device driver
	dev = device_create(dev_class, NULL, MKDEV(MAJOR_NUM, 0), NULL, DEVICE_NAME);
		if(IS_ERR(dev)){
			release_manager();
			class_destroy(dev_class);
			unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
			printk(KERN_ALERT "Failed to create the device\n");
			return PTR_ERR(dev);
   }
	printk("SessionFS driver registered successfully\n");
	return 0;
}

/** Unregister the device, releases the session manager and frees the used memory ( ::dev_ops and ::sess_path)
 */
void release_device(void){
	//unregister the device
	device_destroy(dev_class,MKDEV(MAJOR_NUM,0));
	class_unregister(dev_class);
	class_destroy(dev_class);
	unregister_chrdev(MAJOR_NUM,DEVICE_NAME);
	//free used memory
	release_manager();
	kfree(sess_path);
	kfree(dev_ops);
}
