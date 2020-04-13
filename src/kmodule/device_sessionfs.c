/** \file
 * \brief Implementation of the virtual character device that will handle sessions, component of the _Character Device_ submodule.
 *
 * This file contains the implementation of the device operations that will handle the session semantics and the interaction
 * with the path where sessions are enabled.
 */

#include "device_sessionfs.h"
#include "device_sessionfs_mod.h"
#include "session_manager.h"
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

/// Indicates that the given path is contained in `::sess_path`
#define PATH_OK 1

///Indicates that the device has been disabled and is being removed
#define DEVICE_DISABLED 1

///Lock that protects the session path from concurrent accesses.
rwlock_t dev_lock;

/// The path to the directory in which session sematic is enabled
char* sess_path=NULL;

/// Length of the of the path string
int path_len=0;

/// Indicates that the device must not be used since is being removed.
atomic_t device_status;

/// Refcount of the processes that are using the device
atomic_t refcount;

///File operations allowed on our device
struct file_operations* dev_ops=NULL;

///Device class
struct class* dev_class=NULL;

///Device object
struct device* dev=NULL;

/** \brief Check if the given path is a subpath of `::sess_path`
*
* Gets the dentry from the given path and from  `::sess_path` and checks if the second dentry is an ancestor of the first dentry.
* \param[in] path Path to be checked
* \returns `::PATH_OK` if the given path is a subpath of `::sess_path` and !`::PATH_OK` otherwise; an error code is returned on error.
*
* If the dentry corresponding to the given path cannot be found, the function will check if `::sess_path` is a substring of the given path.
*/
int path_check(const char* path){
	struct path psess,pgiven;
	struct dentry *dsess,*dgiven, *dentry;
	int retval;
	char* p_check=NULL;
	//get dentry from the sess_path
	read_lock(&dev_lock);
	retval=kern_path(sess_path,LOOKUP_FOLLOW,&psess);
	read_unlock(&dev_lock);
	if(retval<0){
		printk(KERN_DEBUG "SessionFS char device: error, can't get %s dentry",sess_path);
		return retval;
	}
	dsess=psess.dentry;
	//get dentry from given path
	retval=kern_path(path,LOOKUP_FOLLOW,&pgiven);
	if(retval<0 && retval!=-ENOENT){
	printk(KERN_DEBUG "SessionFS char device: can't get %s dentry",path);
		return retval;
	}else{
		//we try to find sess_path as a substring of the given path if the file does not exist
		read_lock(&dev_lock);
		printk(KERN_DEBUG "SessionFS char device: %s dentry is non-existent, checking that %s is a substring of the given path",path,sess_path);
		p_check=strstr(path,sess_path);
		read_unlock(&dev_lock);
		if(p_check==NULL){
			return -ENOENT;
		} else{
			return PATH_OK;
		}
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
 * \returns The number of bytes written in `buffer`, or an error code (`-EINVAL` if one of the supplied parameters is invalid,
 *  `-EAGAIN` if the `copy_to_user()` failed, `-ENODEV` if the device is disabled).
 *
 * This function will copy the `::sess_path` content in the supplied buffer.
 * The first operations be executed are the check for the device status on `::device_status`, the incrementation of the `::refcount`.
 * Then `::dev_lock` is grabbed for reading; after the operation is completed `::dev_lock` is released and the `::refcount` is decremented.
 */
static ssize_t device_read(struct file* file, char* buffer,size_t buflen,loff_t* offset){
	int bytes_not_read=0;
	//we check that the device is not closing
	if(atomic_read(&device_status)==DEVICE_DISABLED){
		return -ENODEV;
	}
	atomic_add(1,&refcount);
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen<path_len){
		atomic_sub(1,&refcount);
		return -EINVAL;
	}

	printk(KERN_DEBUG "SessionFS char device: reading session path\n");
	read_lock(&dev_lock);
	bytes_not_read=copy_to_user(buffer,sess_path,path_len);
	read_unlock(&dev_lock);
	atomic_sub(1,&refcount);
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
 * \returns The number of bytes written in `buffer`, or an error code (`-EINVAL` if one of the supplied parameters are invalid,
 * `-EAGAIN` if the copy_from_user failed, `-ENODEV` if the device is disabled).
 *
 * This function will reset and overwrite `::sess_path`, without affecting existing sessions, if the supplied path is absolute.
 * To do so we check `::device_status` and we increment `::refcount`.
 * Then we check that the supplied path starts with '/', grab `::dev_lock` for write operations, we zero-fill `::sess_path`,
 * copy the new string and add a terminator, just in case.
 * Finally we release `::dev_lock` and decrement `::refcount`.
 */
static ssize_t device_write(struct file* file,const char* buffer,size_t buflen,loff_t* offset){
	int bytes_not_written=0;
	char * tmpbuf;
	//we check that the device is not closing
	if(atomic_read(&device_status)==DEVICE_DISABLED){
		return -ENODEV;
	}
	atomic_add(1,&refcount);
	// some basic sanity checks over arguments
	if(buffer==NULL || buflen>PATH_MAX){
		atomic_sub(1,&refcount);
		return -EINVAL;
	}

	//we check that the given path is an absolute path (i.e. it starts with '/')
	tmpbuf=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!tmpbuf){
		atomic_sub(1,&refcount);
		return -ENOMEM;
	}
	bytes_not_written=copy_from_user(tmpbuf,buffer,buflen);
	if(bytes_not_written>0){
		kfree(tmpbuf);
		atomic_sub(1,&refcount);
		return -EINVAL;
	}
	if(tmpbuf[0]!='/'){
		printk(KERN_WARNING "SessionFS char device: relative path specified, session path must be absolute");
		kfree(tmpbuf);
		atomic_sub(1,&refcount);
		return -EINVAL;
	}

	write_lock(&dev_lock);
	printk(KERN_DEBUG "SessionFS char device: changing session path to %s",tmpbuf);
	memset(sess_path,0,sizeof(char)*PATH_MAX);
	memcpy(sess_path,tmpbuf,sizeof(char)*buflen);
	//adding string terminator
	sess_path[PATH_MAX-1]='\0';
	path_len=buflen;
	write_unlock(&dev_lock);
	kfree(tmpbuf);
	atomic_sub(1,&refcount);
	return 0;
}

/** \brief Allows every user to read and write the device file of our virtual device.
 * \param[in] dev Our device struct.
 * \param[out] mode The permissions we set to our device.
 *
 * With this callback that is added to `::dev_ops` we set the permissions of the inode in `/dev` that represents our device,
 * allowing every user to read and write in it.
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

/** \brief Handles the ioctls calls issued to the `SessionFS_dev` device.
 * \param[in] file The special file that represents our char device.
 * \param[in] num The ioctl sequence number, used to identify the operation to be
 * executed, its possible values are `::IOCTL_SEQ_OPEN`, `::IOCTL_SEQ_CLOSE` and `::IOCTL_SEQ_SHUTDOWN`.
 *\param[in,out] param The ioctl param, which is a `::sess_params` struct, that contains the information on the session that must be opened/closed and will be updated with the information on the result of the operation.
 * \returns 0 on success or an error code. (`-ENODEV` if the device is disabled, `-EINVAL` if `path_check()` fails or parameters are invalid, `-EAGAIN` if the `copy_to_user` fails and `-EPIPE` plus a `SIGPIPE` signal if the original file can't be found.)
 *
 * This function checks `::device_status`, increments `::refcount`, then copies the `::sess_params` struct and its `orig_pathname` in kernel space.
 * Its behaviour differs in base of the ioctl sequence number specified:
 * - `::IOCTL_SEQ_OPEN`: Tries to create a session by invoking `create_session()`.
 * 	If the session and the incarnation are created successfully the file descriptor of the incarnation is copied into `::sess_params` `filedes`
 * 	member.
 * 	If the incarnation gets corrupted during creation, the `filedes` member of `::sess_params` is updated as in the successful case, but the corresponding error code is
 * 	returned, so that the library can close and remove the corrupted incarnation file.
 *
 * - `::IOCTL_SEQ_CLOSE`: closes an open session using `close_session()` and the incarnation file must be closed and removed by the library. If
 * the original file does not exist anymore it sends `SIGPIPE` to the user process.
 *
 * - `::IOCTL_SEQ_SHUTDOWN`: disables the device, setting `::device_status` to `::DEVICE_DISABLED`, to avoid race conditions. Then calls
 * `clean_manager()` to check if there are active sessions.
 * 	If there are no active sessions and the refcount is 1 (we are the only process using the device) then the module is unlocked, using
 * 	`module_put()`. Otherwise the device is re-enabled, setting `::device_status` to `!::DEVICE_DISABLED` and the ioctl fails with `-EAGAIN`.
 */
long int device_ioctl(struct file * file, unsigned int num, unsigned long param){
	char* orig_pathname=NULL;
	int res=0,flag,active_sessions=0;
	struct sess_params* p=NULL;
	struct incarnation* inc=NULL;
	struct task_struct* task;
	struct pid* pid;

	printk(KERN_DEBUG "SessionFS char device: received ioctl with num: %d",num);
	//we check that the device is not closing
	if(atomic_read(&device_status)==DEVICE_DISABLED){
		return -ENODEV;
	}
	atomic_add(1,&refcount);
	//we don't need to copy parameters if the shudown is requested
	if(num==IOCTL_SEQ_OPEN || num==IOCTL_SEQ_CLOSE){
		p=kzalloc(sizeof(struct sess_params), GFP_KERNEL);
		if(!p){
			atomic_sub(1,&refcount);
			return -ENOMEM;
		}
		//get the parameters struct from userspace
		res=copy_from_user(p,(struct sess_params*)param,sizeof(struct sess_params));
		if(res>0){
			kfree(p);
			atomic_sub(1,&refcount);
			return -EINVAL;
		}
		// allocating space for the original file pathname
		orig_pathname=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
		if(!orig_pathname){
			kfree(p);
			atomic_sub(1,&refcount);
			return -ENOMEM;
		}
		//copy the pathname string to kernel space
		res=copy_from_user(orig_pathname,p->orig_path,sizeof(char)*PATH_MAX);
		if(res>0){
			kfree(p);
			kfree(orig_pathname);
			atomic_sub(1,&refcount);
			return -EINVAL;
		}
		printk(KERN_DEBUG "SessionFS char device: Copied parameters from userspace");
	}

	switch(num){
		case IOCTL_SEQ_OPEN :
			printk(KERN_DEBUG "SessionFS char device: checking that the original pathname is in %s",sess_path);
			//we check that the original file pathname has sess_path as ancestor
			res=path_check(orig_pathname);
			printk(KERN_DEBUG "SessionFS char device: path_check result: %d",res);
			if(res != PATH_OK){
				kfree(orig_pathname);
				kfree(p);
				atomic_sub(1,&refcount);
				return -EINVAL;
			}
			if(res<0){
				kfree(orig_pathname);
				kfree(p);
				atomic_sub(1,&refcount);
				return res;
			}
			printk(KERN_DEBUG "SessionFS char device: path check ok, checking O_SESS flag presence");
			//we check if the flags include O_SESS and remove to avoid causing trouble for the open function
			if(p->flags & O_SESS){
				flag=p->flags & ~O_SESS;
			}else {
				kfree(orig_pathname);
				kfree(p);
				atomic_sub(1,&refcount);
				return -EINVAL;
			}
			printk(KERN_DEBUG "SessionFS char device: flag check ok, creating session");
			//we create a new session incarnation
			inc=create_session(orig_pathname,flag,p->pid,p->mode);
			//return the error if we have failed in creating the session
			if(IS_ERR(inc) || inc==NULL){
				kfree(p);
				kfree(orig_pathname);
				atomic_sub(1,&refcount);
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
			if(res>0){
				printk(KERN_DEBUG "SessionFS char device: bytes not copied to userspace: %d, size of strct sess_params: %ld",res, sizeof(struct sess_params));
				atomic_sub(1,&refcount);
				return -EAGAIN;
			}
			printk(KERN_INFO "SessionFS char device: session creation successful, session status: %d\n",inc->status);
			res=inc->status;
			break;

		case IOCTL_SEQ_CLOSE :
			printk(KERN_INFO "SessionFS char device: closing an active incarnation");
			res=close_session(orig_pathname,p->filedes,p->pid);
			kfree(orig_pathname);
			if(res<0){
				printk(KERN_INFO "SessionFS char device: failed closing the incarnation, sending SIGPIPE");
				//we get the task struct of the user process
				pid=find_get_pid(p->pid);
				if(IS_ERR(pid) || pid==NULL){
					atomic_sub(1,&refcount);
					return -EPIPE;
				}
				task=get_pid_task(pid,PIDTYPE_PID);
				if(task == NULL || IS_ERR(task)){
					atomic_sub(1,&refcount);
					return -EPIPE;
				}
				//we send the SIGPIPE
				res=send_sig(SIGPIPE,task,0);
				atomic_sub(1,&refcount);
				return -EPIPE;
			}
			kfree(p);
			printk(KERN_INFO "SessionFS char device: closed incarnation successfully");
			break;

		case IOCTL_SEQ_SHUTDOWN :
			printk(KERN_INFO "SessionFS char device: requesting device shutdown");
			//we disable the device to avoid having other preocesses using it
			atomic_set(&device_status,DEVICE_DISABLED);
			//we try to clean the session manager
			active_sessions=clean_manager();
			//we write to the user the number of active sessions
			res=copy_to_user((int*)param,&active_sessions,sizeof(int));
			if(res>0){
				printk(KERN_DEBUG "SessionFS char device: bytes not copied to userspace: %d",res);
			}
			printk(KERN_DEBUG "SessionFS char device: refcount %d,active_sessions: %d,kobject refcount: %d ",atomic_read(&refcount),active_sessions,kref_read(&(dev->kobj.kref)));
			/// To allow the unload of the module we need to have no active sessions, no processes that are using the device and no processes that are using the device kobject.
			if(active_sessions==0 && atomic_read(&refcount)==1 && kref_read(&(dev->kobj.kref))==2){
				//we wait for the rcu items to be deallocated
				synchronize_rcu();
				printk(KERN_INFO "SessionFS char device: shutdown allowed, module unlocked");
				// since we are the only ones using the device we can safely unlock it while maintaing it disabled.
				module_put(THIS_MODULE);
			} else {
				printk(KERN_INFO "SessionFS char device: shutdown not allowed, device is in use");
				//we re-enable the device seince we cannot shut it down while is in use
				atomic_set(&device_status,!DEVICE_DISABLED);
				res= -EAGAIN;
			}
			break;
	}
	atomic_sub(1,&refcount);
	return res;
}

/** Initializes and registers the device by setting `::sess_path`, `::path_len` variables:
 * `::dev_ops` will contain the operations allowed on the device, which are `device_ioctl()`, `device_read()` and `device_write()`, and the `sessionfs_devnode()` callback to set the inode permissions.
 * The _Session Manager_ submodule is also initialized using  `init_manager()` and the same happens for the
 * _Session Information_ submodule, using `init_info()`, after the device is registered.
 * Finally we lock the module with `try_module_get()` to prevent it being unmounted while is in use.
 */
int init_device(void){
	int res;
	//we initilize the flag of the device
	atomic_set(&device_status,!DEVICE_DISABLED);
	//we initialize the refcount
	atomic_set(&refcount,0);
	//we initialize the read-write lock
	rwlock_init(&dev_lock);
	// allocate the path buffer and path_len
	sess_path=kzalloc(PATH_MAX*sizeof(char),GFP_KERNEL);
	strcpy(sess_path,DEFAULT_SESS_PATH);
	path_len=strlen(DEFAULT_SESS_PATH);
	//allocate and initialize the dev_ops struct
	dev_ops= kzalloc(sizeof(struct file_operations),GFP_KERNEL);
	dev_ops->owner=THIS_MODULE;
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
	//finally we lock the module
	try_module_get(THIS_MODULE);
	return 0;
}

/** Unregisters the device, cleans the _Session Manager_ just to be sure to avoid memory leaks, releases the _Session Information_ and frees the used memory ( `::dev_ops` and `::sess_path`).
 */
void release_device(void){
	//device disable and manager clean are run again here since the module can be forced to be removed
	atomic_set(&device_status,DEVICE_DISABLED);
	clean_manager();
	printk(KERN_DEBUG "SessionFS char device: releasing the device resources");
	//we check if there are active incarnations
	printk(KERN_DEBUG "SessionFS char device: unregistering device and freeing used memory");
	//remove the info on sessions
	release_info();
	printk(KERN_DEBUG "SessionFS char device: destroying and unregistering the device");
	//unregister the device
	device_destroy(dev_class,MKDEV(MAJOR_NUM,0));
	class_destroy(dev_class);
	unregister_chrdev(MAJOR_NUM,DEVICE_NAME);
	//free used memory
	kfree(sess_path);
	kfree(dev_ops);
	printk("SessionFS char device: device release complete");
}
