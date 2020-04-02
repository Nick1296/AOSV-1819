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
//for find_get_pid
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

///Indicates that the device has been disabled and is being removed
#define DEVICE_DISABLED 1

///Lock that protects the session path from concurrent accesses.
rwlock_t dev_lock;

/// \brief Parameter that keeps the path to the directory in which session sematic is enabled
char* sess_path=NULL;

/// length of the of the path string
int path_len=0;

/// Parameter that indicates that the device must not be used since is being removed \todo check if this really needs to be an atomic_t.
atomic_t device_disabled;

/// Paramters that indicates if the module can has already been locked, to avoid setting a module dependecy multiple times
atomic_t module_locked;

/// Refcount of the processes that are using the device
atomic_t refcount;

///File operations allowed on our device
struct file_operations* dev_ops=NULL;

///Device class
struct class* dev_class=NULL;

///Device object
struct device* dev=NULL;

/** \brief Check if the given path is a subpath of ::sess_path
*
* Gets the dentry from the given path and from  ::sess_path and check is the second dentry is an ancestor of the first dentry.
* \param[in] path Path to be checked
* \returns ::PATH_OK if the given path is a subpath of ::sess_path and !::PATH_OK otherwise; an error code is returned on error.
*/
int path_check(const char* path){
	struct path psess,pgiven;
	struct dentry *dsess,*dgiven, *dentry;
	int retval;
	char* p_check=NULL;
	//get dentry from the sess_path
	read_lock(&dev_lock);
	retval=kern_path(sess_path,LOOKUP_FOLLOW,&psess);
	if(retval!=0){
		return retval;
	}
	printk(KERN_DEBUG "SessionFS char device: got %s dentry",sess_path);
	dsess=psess.dentry;
	//get dentry from given path
	retval=kern_path(path,LOOKUP_FOLLOW,&pgiven);
	if(retval<0 && retval!=-ENOENT){
		return retval;
	}else{
		/// \todo check is this is a valid solution
		//we try to find sess_path as a substring of the given path
		p_check=strstr(path,sess_path);
		read_unlock(&dev_lock);
		if(p_check==NULL){
			return -ENOENT;
		} else{
			return PATH_OK;
		}
	}
	read_unlock(&dev_lock);
	printk(KERN_DEBUG "SessionFS char device: got %s dentry",path);
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

/** \brief Locks the module, avoiding rmmod while the device is working.
 * To do so it uses the try_module_get to set a single module dependence.
 */
void lock_module(void){
	if(atomic_read(&module_locked)==0){
		atomic_add(1,&module_locked);
		try_module_get(THIS_MODULE);
	}
	atomic_add(1,&refcount);
}

/** \brief Unlocks the module, allowing rmmods.
 * To do so it uses the module_put to remove the module dependence.
 */
void unlock_module(void){
	atomic_sub(1,&refcount);
	if(atomic_read(&module_locked)==1 && atomic_read(&refcount)==0 && clean_manager()==0){
		atomic_sub(1,&module_locked);
		module_put(THIS_MODULE);
	}
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
	lock_module();
	//we check that the device is not closing
	if(atomic_read(&device_disabled)==DEVICE_DISABLED){
		unlock_module();
		return -ENODEV;
	}
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen<path_len){
		unlock_module();
		return -EINVAL;
	}

	printk(KERN_DEBUG "SessionFS char device: read locking dev_lock for device_read");
	read_lock(&dev_lock);
	printk(KERN_DEBUG "SessionFS char device: reading session path\n");
	bytes_not_read=copy_to_user(buffer,sess_path,path_len);
	read_unlock(&dev_lock);
	printk(KERN_DEBUG "SessionFS char device: read releasing dev_lock for device_read");
	unlock_module();
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
	char * tmpbuf;
	lock_module();
	//we check that the device is not closing
	if(atomic_read(&device_disabled)==DEVICE_DISABLED){
		unlock_module();
		return -ENODEV;
	}

	// some basic sanity checks over arguments
	if(buffer==NULL || buflen>PATH_MAX){
		unlock_module();
		return -EINVAL;
	}

	//we check that the given path is an absolute path (i.e. it starts with '/')
	tmpbuf=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!tmpbuf){
		unlock_module();
		return -ENOMEM;
	}
	bytes_not_written=copy_from_user(tmpbuf,buffer,buflen);
	if(bytes_not_written>0){
		kfree(tmpbuf);
		unlock_module();
		return -EINVAL;
	}
	if(tmpbuf[0]!='/'){
		printk(KERN_WARNING "SessionFS char device: relative path specified, session path must be absolute");
		kfree(tmpbuf);
		unlock_module();
		return -EINVAL;
	}

	printk(KERN_DEBUG "SessionFS char device: write locking dev_lock for device_write");
	write_lock(&dev_lock);
	memset(sess_path,0,sizeof(char)*PATH_MAX);
	memcpy(sess_path,tmpbuf,sizeof(char)*buflen);
	//adding string terminator
	sess_path[PATH_MAX-1]='\0';
	path_len=buflen;
	write_unlock(&dev_lock);
	printk(KERN_DEBUG "SessionFS char device: write releasing dev_lock for device_write");
	kfree(tmpbuf);
	unlock_module();
	return 0;
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
 * This function copies the ::sess_params struct in kernel space and cleans the userspace string in sess_params::inc_pathname.
 * Its behaviour differs in base of the ioctl sequence number specified:
 * * ::IOCTL_SEQ_OPEN: copies the pathname of the original file in kernel space and tries to create a session, by invoking ::create_session.
 * If the session and the incarnation are created succesfully the file descriptor of the incarnation is copied into sess_params::pid.
 * If the incarnation gets corrupted during creation, the pid is updated as in the successful case, but the incarnation pathname is copied into sess_params::inc_path and the corresponding error code is returned, so that the library can close and remove the corrupted incarnation file.
 * * ::IOCTL_SEQ_CLOSE: closes an open session using ::close_session and updates the ::sess_params struct with the path of the incarnation file which must be closed and removed by the library. If the original file does not exist anymore it sends `SIGPIPE` to the user process.
 */
long int device_ioctl(struct file * file, unsigned int num, unsigned long param){
	char* orig_pathname=NULL;
	int res=0,flag;
	struct sess_params* p=NULL;
	struct incarnation* inc=NULL;
	struct task_struct* task;
	struct pid* pid;

	lock_module();
	//we check that the device is not closing
	if(atomic_read(&device_disabled)==DEVICE_DISABLED){
		unlock_module();
		return -ENODEV;
	}

	p=kzalloc(sizeof(struct sess_params), GFP_KERNEL);
	if(!p){
		unlock_module();
		return -ENOMEM;
	}
	//get the parameters struct from userspace
	res=copy_from_user(p,(struct sess_params*)param,sizeof(struct sess_params));
	if(res>0){
		kfree(p);
		unlock_module();
		return -EINVAL;
	}
	// allocating space for the original file pathname
	orig_pathname=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!orig_pathname){
		kfree(p);
		unlock_module();
		return -ENOMEM;
	}
	printk(KERN_INFO "SessionFS char device: creating a new session");
	//copy the pathname string to kernel space
	res=copy_from_user(orig_pathname,p->orig_path,sizeof(char)*PATH_MAX);
	if(res>0){
		kfree(p);
		kfree(orig_pathname);
		unlock_module();
		return -EINVAL;
	}

	printk(KERN_DEBUG "SessionFS char device:Copied parameters from userspace");
	switch(num){
		case IOCTL_SEQ_OPEN:
			printk(KERN_DEBUG "SessionFS char device: checking that the original pathname is in %s",sess_path);
			//we check that the original file pathname has ::sess_path as ancestor
			res=path_check(orig_pathname);
			printk(KERN_DEBUG "SessionFS char device: path_check result: %d",res);
			if(res != PATH_OK){
				kfree(orig_pathname);
				kfree(p);
				unlock_module();
				return -EINVAL;
			}
			if(res<0){
				kfree(orig_pathname);
				kfree(p);
				unlock_module();
				return res;
			}
			printk(KERN_DEBUG "SessionFS char device: path check ok, checking O_SESS flag presence");
			//we check if the flags include O_SESS and remove to avoid causing trouble for the open function
			if(p->flags & O_SESS){
				flag=p->flags & ~O_SESS;
			}else {
				kfree(orig_pathname);
				kfree(p);
				unlock_module();
				return -EINVAL;
			}
			printk(KERN_DEBUG "SessionFS char device: flag check ok, creating session");
			//we create a new session incarnation
			inc=create_session(orig_pathname,flag,p->pid);
			//return the error if we have failed in creating the session
			if(IS_ERR(inc) || inc==NULL){
				kfree(p);
				kfree(orig_pathname);
				unlock_module();
				return (IS_ERR(inc)) ? PTR_ERR(inc) : -EAGAIN ;
			}
			//the validity of the session is set by the status of the incarnation
			p->valid=inc->status;
			printk(KERN_DEBUG "SessionFS char device: copying parameters to userspace");
			//we set the file descriptor into the sess_struct.
			p->filedes=inc->filedes;
			//we overwrite the existing sess_struct in userspace
			res=copy_to_user((struct sess_params*)param,p,sizeof(struct sess_params));
			kfree(p);
			printk(KERN_DEBUG "SessionFS char device: bytes not copied to userspace: %d, size of strct sess_params: %ld",res, sizeof(struct sess_params));
			if(res>0){
				unlock_module();
				return -EAGAIN;
			}
			printk(KERN_INFO "SessionFS char device: session creation successful, session status: %d\n",inc->status);
			res=inc->status;
			break;

		case IOCTL_SEQ_CLOSE:
			printk(KERN_INFO "SessionFS char device: closing an active incarnation");
			res=close_session(orig_pathname,p->filedes,p->pid);
			kfree(orig_pathname);
			if(res<0){
				printk(KERN_INFO "SessionFS char device: failed closing the incarnation, sending SIGPIPE");
				//we get the task struct of the user process
				pid=find_get_pid(p->pid);
				if(IS_ERR(pid) || pid==NULL){
					unlock_module();
					return -EPIPE;
				}
				task=get_pid_task(pid,PIDTYPE_PID);
				if(task == NULL || IS_ERR(task)){
					unlock_module();
					return -EPIPE;
				}
				//we send the SIGPIPE
				res=send_sig(SIGPIPE,task,0);
				unlock_module();
				return -EPIPE;
			}
			kfree(p);
			printk(KERN_INFO "SessionFS char device: closed incarnation successfully");
			break;
	}
	unlock_module();
	return res;
}

/** Initializes the devices by setting sess_path, path_len and dev_ops variables, then registers the devices.
 */
int init_device(void){
	int res;
	//we initilize the flag of the device
	atomic_set(&device_disabled,!DEVICE_DISABLED);
	//we initialize the refcount
	atomic_set(&refcount,0);
	//we initialize the refcount
	atomic_set(&module_locked,0);
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
		printk(KERN_ALERT "SessionFS char device: failed to register the sessionfs virtual device\n");
		return res;
	}
	printk(KERN_INFO "SessionFS char device: Device %s registered\n", DEVICE_NAME);
	//register the device class
	dev_class=class_create(THIS_MODULE,CLASS_NAME);
	if (IS_ERR(dev_class)){
		unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
		printk(KERN_ALERT "SessionFS char device: Failed to register device class\n");
		return PTR_ERR(dev_class);
	}
	//setting devnode
	dev_class->devnode=sessionfs_devnode;
	printk("SessionFS char device: SessionFS device class registered successfully\n");
	//register the device driver
	dev = device_create(dev_class, NULL, MKDEV(MAJOR_NUM, 0), NULL, DEVICE_NAME);
		if(IS_ERR(dev)){
			class_destroy(dev_class);
			unregister_chrdev(MAJOR_NUM, DEVICE_NAME);
			printk(KERN_ALERT "SessionFS char device: Failed to create the device\n");
			return PTR_ERR(dev);
   }
	printk(KERN_INFO "SessionFS char device: SessionFS driver registered successfully\n");
	init_info(&(dev->kobj));
	return 0;
}

/** Unregister the device, releases the session manager and frees the used memory ( ::dev_ops and ::sess_path)
 */
void release_device(void){
	printk(KERN_DEBUG "SessionFS char device: releasing the device resources");
	//we flag the device as disabled
	atomic_set(&device_disabled,DEVICE_DISABLED);
	printk(KERN_DEBUG "SessionFS char device: requesting session manager release");
	clean_manager();
	//we check if there are active incarnations
	printk(KERN_INFO "SessionFS char device: unregistering device and freeing used memory");
	//remove the info on sessions
	release_info();
	//unregister the device
	device_destroy(dev_class,MKDEV(MAJOR_NUM,0));
	class_unregister(dev_class);
	class_destroy(dev_class);
	unregister_chrdev(MAJOR_NUM,DEVICE_NAME);
	//free used memory
	kfree(sess_path);
	kfree(dev_ops);
	printk("SessionFS char device: device release complete");
}
