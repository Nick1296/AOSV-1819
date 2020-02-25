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
///The module that handles the infornation on sessions
#include "session_info.h"

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
//for get_pid_task
#include <linux/pid.h>
//for struct task_struct
#include <linux/sched.h>
//for spinlock APIs
#include <linux/spinlock.h>
//for signal apis
#include <linux/sched/signal.h>

// dentry management
#include<linux/namei.h>
#include<linux/path.h>
#include<linux/dcache.h>

///The default session path when the device is initialized
#define DEFAULT_SESS_PATH "/mnt"

/// Indicates that the given path is contained in ::sess_path
#define PATH_OK 1

///Lock that protects the session path from concurrent accesses.
rwlock_t dev_lock;

/// \brief parameter that keeps the path to the directory in which session sematic is enabled
///\todo TODO check if this varaible must be protected from concurrent access
char* sess_path=NULL;

/// length of the of the path string
///\todo TODO check if this variable must be protected from concurrent access
int path_len=0;

///File operations allowed on our device
struct file_operations* dev_ops=NULL;

///Device class
struct class* dev_class=NULL;

///Device object
struct device* dev=NULL;

/** \brief Check if the given path is a subpath of \ref sess_path
*
* Gets the dentry from the given path and from  \ref sess_path and check is the second dentry is an ancestor of the first dentry.
* \todo implement!
* \param[in] path Path to be checked
* \returns 1 if the given path is a subpath of \ref sess_path and 0 otherwise; -1 is returned on error.
*/
int path_check(const char* path){
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
			return PATH_OK;
		} else{
			dentry=dentry->d_parent;
		}
	}
	return 0;
}

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
	printk(KERN_DEBUG "reading session path\n");
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen<path_len){
		return -EINVAL;
	}
	printk(KERN_DEBUG "read locking dev_lock");
	read_lock(&dev_lock);
	bytes_not_read=copy_to_user(buffer,sess_path,path_len);
	read_unlock(&dev_lock);
	printk(KERN_DEBUG "read releasing dev_lock");
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
	if(buffer==NULL || buflen>PATH_MAX){
		return -EINVAL;
	}
	printk(KERN_DEBUG "write locking dev_lock");
	write_lock(&dev_lock);
	bytes_not_written=copy_from_user(sess_path,buffer,buflen);
	path_len=buflen;
	write_unlock(&dev_lock);
	printk(KERN_DEBUG "write locking dev_lock");
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
 * \param[in] file The special file that represents our char device.
 * \param[in] num The ioctl sequence number, used to identify the operation to be
 * executed, its possible values are ::IOCTL_SEQ_OPEN and ::IOCTL_SEQ_CLOSE.
 *\param[in,out] param The ioctl param, which is a ::sess_params struct, that contains the information on the session that must be opened/closed and will be update with the information on the result of the operation.
 * \returns 0 on success or an error code.
 *
 * This function copies the ::sess_params struct in kernel space and cleans the userspace string in sees_params::inc_pathname.
 * Its behaviour differs in base of the ioctl sequence number specified:
 * * ::IOCTL_SEQ_OPEN: copies the pathname of the original file in kernel space and tries to create a session, by invoking ::create_session.
 * If the session and the incarnation are created succesfully the file descriptor of the incarnation is copied into sess_params::pid.
 * If the incarnation gets corrupted during creation, the pid is updated as in the successful case, but the incarnation pathname is copied into sess_params::inc_path and the corresponding error code is returned, so that the library can close and remove the corrupted incarnation file.
 * * ::IOCTL_SEQ_CLOSE: closes an open session using ::close_session and updates the ::sess_params struct with the path of the incarnation file which must be closed and removed by the library. If the original file does not exist anymore it sends `SIGPIPE` to the user process.
 */
long int device_ioctl(struct file * file, unsigned int num, unsigned long param){
	char* orig_pathname=NULL;
	const  char* inc_pathname=NULL;
	int res=0;
	int flag;
	struct sess_params* p=NULL;
	struct incarnation* inc=NULL;
	struct task_struct* task;
	struct pid* pid;
	p=kzalloc(sizeof(struct sess_params), GFP_KERNEL);
	if(!p){
		return -ENOMEM;
	}
	//get the parameters struct from userspace
	res=copy_from_user(p,(struct sess_params*)param,sizeof(struct sess_params));
	if(res>0){
		kfree(p);
		return -EINVAL;
	}

	// allocating space for the original file pathname
	orig_pathname=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!orig_pathname){
		kfree(p);
		return -ENOMEM;
	}

	switch(num){
		case IOCTL_SEQ_OPEN:
			//copy the pathname string to kernel space
			res=copy_from_user(orig_pathname,p->orig_path,sizeof(char)*PATH_MAX);
			if(res>0){
				kfree(p);
				kfree(orig_pathname);
				return -EINVAL;
			}
			//we check that the orginal file pathname has ::sess_path as ancestor
			res=path_check(orig_pathname);
			if(res != PATH_OK){
				kfree(orig_pathname);
				kfree(p);
				return -EINVAL;
			}
			if(res<0){
				kfree(orig_pathname);
				kfree(p);
				return res;
			}
			//we check if the flags include O_SESS and remove to avoid causing trouble for the open function
			if(p->flags & O_SESS){
				flag=p->flags & ~O_SESS;
			}else {
				return -EINVAL;
			}
			//we create a new session incarnation
			inc=create_session(orig_pathname,flag,p->pid);
			//return the error if we have failed in creating the session
			if(IS_ERR(inc)){
				kfree(p);
				kfree(orig_pathname);
				return PTR_ERR(inc);
			}
			//now we must check that the created session is valid
			if(inc->status<0){
				p->valid=inc->status;
				//we copy the incarnation pathname into the corresponding parameter in the sess_struct.
				/// \todo check is this is really necessary (it shouldn't be needed)
				res=copy_to_user(p->inc_path,inc->pathname,sizeof(char)*PATH_MAX);
				if(res>0){
					//this should not happen since we have alredy tried to copy into this struct at the beginning.
					kfree(orig_pathname);
					kfree(p);
					return -EINVAL;
				}
			}
			//we flag the incarnation as valid.
			p->valid=VALID_SESS;
			//we set the file descriptor into the sess_struct.
			p->filedes=inc->filedes;
			//we overwrite the existing sess_struct in userspace
			res=copy_to_user((struct sess_params*)param,p,sizeof(struct sess_params));
			kfree(orig_pathname);
			kfree(p);
			if(res>0){
				return -EAGAIN;
			}
			res=inc->status;
			break;

		case IOCTL_SEQ_CLOSE:
			//we try to initialize the sess_params::inc_pathname with a sequence of 0, to see if it is a valid userspace memory address
			res=copy_to_user(p->inc_path,orig_pathname,sizeof(char)*PATH_MAX);
			kfree(orig_pathname);
			if(res>0){
				kfree(p);
				return -EINVAL;
			}
			res=close_session(p->filedes,p->pid,&inc_pathname);
			kfree(inc_pathname);
			if(res<0){
				//we get the task struct of the user process
				pid=find_get_pid(p->pid);
				if(IS_ERR(pid) || pid==NULL){
					return -ESRCH;
				}
				task=get_pid_task(pid,PIDTYPE_PID);
				if(task == NULL || IS_ERR(task)){
					return -ESRCH;
				}
				//we send the SIGPIPE
				res=send_sig(SIGPIPE,task,0);
				/// \todo TODO check how send_sig works and what it returns
			}
			//we give the incarnation pathname to the usersoace so the library can remove it
			res=copy_to_user(p->inc_path,inc_pathname,sizeof(char)*PATH_MAX);
			kfree(inc_pathname);
			if(res>0){
				//this should not happen since we have already tried to copy into this struct at the beginning.
				kfree(p);
				return -EAGAIN;
			}
			res=copy_to_user((struct sess_params*)param,p,sizeof(struct sess_params));
			kfree(p);
			if(res>0){
				return -EAGAIN;
			}
			break;
	}
	return res;
}

/** Initializes the devices by setting sess_path, path_len and dev_ops variables, then registers the devices.
 */
int init_device(void){
	int res;
	//we initialize the read-write lock
	rwlock_init(&dev_lock);
	// allocate the path buffer and path_len
	sess_path=kzalloc(PATH_MAX*sizeof(char),GFP_KERNEL);
	strcpy(sess_path,DEFAULT_SESS_PATH);
	path_len=strlen(DEFAULT_SESS_PATH);
	//allocate and initialize the dev_ops struct
	dev_ops= kzalloc(sizeof(struct file_operations),GFP_KERNEL);
	dev_ops->read=device_read;
	dev_ops->write=device_write;
	dev_ops->unlocked_ioctl=device_ioctl;
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
	init_info(&(dev->kobj));
	return 0;
}

/** Unregister the device, releases the session manager and frees the used memory ( ::dev_ops and ::sess_path)
 */
void release_device(void){
	//remove the info on sessions
	release_info();
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
