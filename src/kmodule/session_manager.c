/** \file session_manager.c
 * \brief Implementation of the _Session Manager_ submodule.
 *
 * This file contains the implementation of the session manager, which will handle the creation and deletion of a session
 * keeping track of the opened sessions for each file.
 */

// for the list_head struct and rcu_head
#include <linux/types.h>
// for PATH_MAX
#include<uapi/linux/limits.h>
//for read-write locks and spinlocks APIs
#include <linux/spinlock.h>
//for lockless list APIs
#include <linux/llist.h>
//for simple lists APIs
#include <linux/list.h>
//for list using the rcu APIs
#include <linux/rculist.h>
//fot file access APIs
#include <linux/fs.h>
//for file descriptors APIs
#include <linux/file.h>
//for memory APIs
#include <linux/slab.h>
//error managemnt macros
#include <linux/err.h>
//for errno numbers
#include <uapi/asm-generic/errno.h>
//for timestamps
#include <linux/timekeeping.h>
//for the O_* flags
#include <uapi/asm-generic/fcntl.h>
//for fsnotify_open
#include <linux/fsnotify.h>
//for find_get_pid
#include <linux/pid.h>

#include "session_manager.h"

#include "session_info.h"


/// Used to toggle the necessity of a file descriptor in `open_file()` and to determine the type of search in `search_session()`.
#define NO_FD 0

/// Used to determinethe type of search in `search_session()`.
#define NO_PID 0

///Permissions to be given to the newly created files.
#define DEFAULT_PERM 0644

///Used to determine if a session node is valid.
#define VALID_NODE 0

///The portion of the file which is copied at each read/write iteration
#define DATA_DIM 512

///Used to determine if the content of the incarnation must overwrite the original file on close
#define OVERWRITE_ORIG 0

///Used to determine is the session manager contains active sessions.
#define MANAGER_EMPTY 0

///Return value of pid_alive (in `linux/sched.h`) if the process is still alive
#define ALIVE 1

///List of the active `::session`(s).
struct list_head sessions;

///Spinlock used to update the list of the active `::session`(s)
spinlock_t sessions_lock;

/** \brief Opens a file from kernel space.
 * \param[in] pathname String that represents the file location and name and __must be in kernel memory__
 * \param[in] flags Flags that will regulate the permissions on the file.
 * \param[in] mode The permissions to apply to newly created files.
 * \param[in] fd_needed If set to `::NO_FD` the file descriptor for the openend file will not be assigned (used only when we are opening original files).
 * \param[out] file pointer to the file struct of the opened file.
 * \returns The file descriptor on success, the error code or `::NO_FD` if no file descriptor was requested.
 *
 * This function will open the file at the specified pathname and then associate a file descriptor to the opened file.
 * If the `O_CREAT` flags is specified the permissions for the newly created file will be defined by the `mode` parameter or,
 * if this is -1 by `::DEFAULT_PERM`.
 * If `fd_needed` is set to `::NO_FD` then the opened file won't have a file descriptor associated.
 */
int open_file(const char* pathname, int flags, mode_t mode,int fd_needed, struct file** file){
	struct file* f=NULL;
	int fd;
	mode_t perms;
	if(mode==-1){
		perms=DEFAULT_PERM;
	} else {
		perms=mode;
	}
	printk(KERN_DEBUG "SessionFS session manager: opening (and creating it if needed): %s",pathname);
	//we try to open the file
	f=filp_open(pathname,flags,perms);
	if (IS_ERR(f)) {
		fd = PTR_ERR(f);
	} else {
		printk(KERN_DEBUG "SessionFS session manager: file opened successfully");
		if(fd_needed){
			//find a new file descriptor
			fd=get_unused_fd_flags(flags);
			if(fd>=0){
				//notify processes that a file has been opened
				fsnotify_open(f);
				//register the descriptor in the intermediate table
				fd_install(fd, f);
				printk(KERN_DEBUG "SessionFS session manager: associated file with file descriptor: %d",fd);
			}
		} else {
			fd=NO_FD;
		}
	}
	*file=f;
	return fd;
}

/** \brief Searches for a `::session` with a given pathname, or with an `::incarnation` with matching pid and file descriptor.
 * \param[in] pathname The pathname that identifies the session.
 * \param[in] filedes The file descriptor of an incarnation.
 * \param[in] pid The pid of the process that owns the incarnation.
 * \returns A pointer to the found `::session` or `NULL`.
 *
 * Searches, by navigating the `::sessions` rcu list, a `::session` which matches the given `pathname`.
 * If `pathname` is NULL, `filedes` is differrent from `::NO_FD` and `pid` is differrent from `::NO_PID`
 * then it searches for the session that contains an incarnation with the corresponding pid an file descriptor.
 * while walking the `::sessions` list the reference counter counter of any session currently inspeced, `refcount`, will be incremented, to minimize the possibility of returning an invalid session. If the session is not the one we are looking for then `refcount` will be decremented before inspecting the next `::session`. If a `::session` is invalid it will be skipped.
 * Additionally while walking the `::incarnation` list of a `::session` we grab the `sess_lock` in read mode, to avoid
 * that a concurrent process modifies the list while we are walking it. When we finish walking the list we will release the lock.
 */
struct session* search_session(const char* pathname,int filedes, pid_t pid){
	struct incarnation *inc_it,*inc_tmp;
	struct session_rcu *session_rcu_it=NULL;
	struct session *session_it=NULL,*found=NULL;
	//paramters check
	if(pathname==NULL && filedes==NO_FD){
		return NULL;
	}
	printk(KERN_DEBUG "SessionFS session manager: searching for a session with an incarnation with pathname:%s, pid:%d and fd:%d",pathname, pid, filedes);
	//we get the read lock on the rcu
	rcu_read_lock();
	//we get the first element of the session list
	session_rcu_it= list_first_or_null_rcu(&sessions,struct session_rcu,list_node);
	if(session_rcu_it==NULL){
		printk(KERN_DEBUG "SessionFS session manager: session list empty on search");
		rcu_read_unlock();
		return NULL;
	}
	//we need to walk all the list to see if we have alredy other sessions opened for the same pathname
	list_for_each_entry_rcu(session_rcu_it,&sessions,list_node,NULL){
		session_it=session_rcu_it->session;
		//we increment the refcount
		atomic_add(1,&(session_it->refcount));
		if(session_it->valid==VALID_NODE){
			if(pathname!=NULL && strcmp(session_it->pathname,pathname) == 0){
				printk(KERN_DEBUG "SessionFS session manager: found session by pathname");
				found=session_it;
			}
			if(pathname==NULL && filedes != NO_FD && pid!=NO_PID ){
				printk("SessionFS session manager: searching incarnation in session %s",found->pathname);
				read_lock(&(session_it->sess_lock));
				llist_for_each_entry_safe(inc_it,inc_tmp,session_it->incarnations.first,node){
					if(inc_it->owner_pid==pid && inc_it->filedes==filedes){
						printk(KERN_DEBUG "SessionFS session manager: found session by incarnation pid and file descriptor");
						found=session_it;
						break;
					}
				}
				read_unlock(&(found->sess_lock));
			}
		} else {
			printk(KERN_DEBUG "SessionFS session manager: found an invalid session during search, skipping");
		}
		if(found!=NULL){
			break;
		} else{
			atomic_sub(1,&(session_it->refcount));
		}
	}
	rcu_read_unlock();
	return found;
}

/** \brief Deallocates the given session object.
 * \param[in] session The session object to deallocate.
 *
 * This function is used to free the memory used by the session when nobody is accessing it, this is checked sing the `::session` `refcount` member.
 * The method will attempt to deallocate the session object, if the refcount is 0 (only the current process is using it).
 * Otherwise the method will do nothing.
 *
 * If the `::session` object is going to be deallocated SysFS is updated with `remove_session_info()`.
 */
void delete_session(struct session* session){
	printk(KERN_DEBUG "SessionFS session manager: checking is someone is using the session object");
	if(atomic_read(&(session->refcount))>0){
		printk(KERN_WARNING "SessionFS session manager: session in use, cannot eliminate the object");
	} else {
		printk(KERN_INFO "SessionFS session manager: session object not in use, proceeding with elimination");
		remove_session_info(&(session->info));
		filp_close(session->file,NULL);
		kfree(session->pathname);
		session->file=NULL;
		session->pathname=NULL;
		kfree(session);
	}
}

/**
 * \brief Initializes the session information for the given pathname.
 * \param[in] pathname The path of the original file.
 * \param[in] flags The flags that regulate the access to the original file.
 * \param[in] mode The permissions to apply to newly created files.
 *
 * To create a new `::session` object we get the spinlock `::sess_lock` to avoid race coditions while creating the `::session`.
 * Then a search is issued,useing `search_session()`, to see if there is already a matching session with the same `pathname` to be returned.
 * If the search gives an invalid object or `NULL` we proceed in the session creation, calling `add_session_info()`,
 * opening the file, using `open_file()`, creating the object and adding it in the `::sessions` list and finally we release the spinlock.
 * The original flags will be modified by removing the `O_RDONLY` and `O_WRONLY` in favor of `O_RDWR`, since we will always
 * read and write on this file.
 * With the specified flags we preserve the semantic of the `O_EXCL` flag.
 */
struct session* init_session(const char* pathname,int flags, mode_t mode){
	struct file* file=NULL;
	int fd=NO_FD;
	int res=0;
	int flag;
	struct session *node=NULL, *node_f=NULL;
	struct session_rcu* node_rcu;
	//we allocate the rcu node that will hold the session object
	node_rcu=kmalloc(sizeof(struct session_rcu), GFP_KERNEL);
	if(!node_rcu){
		return ERR_PTR(-ENOMEM);
	}
	//we allocate the new session object
	node=kmalloc(sizeof(struct session),GFP_KERNEL);
	if(!node){
		kfree(node_rcu);
		return ERR_PTR(-ENOMEM);
	}
	printk(KERN_DEBUG "SessionFS session manager: successfully allocated necessary memory");
	//we get the spinlock to avoid race conditions while creating the session object
	spin_lock(&sessions_lock);
	printk(KERN_DEBUG "SessionFS session manager: checking for an already existing session with the same pathname: %s",pathname);
	//we check if, while we were searching, the session has been already created
	node_f=search_session(pathname,NO_FD,NO_FD);
	if(node_f!=NULL){
		printk(KERN_DEBUG "SessionFS session manager: found an already existing session");
		if(node_f->valid!=VALID_NODE){
			printk(KERN_DEBUG "SessionFS: session manager: Found session is invalid, continuing with creation");
			atomic_sub(1,&(node_f->refcount));
			delete_session(node_f);
		}
		//we return the found session;
		spin_unlock(&sessions_lock);
		return node_f;
	}
	//we update the info on the device kobject
	res=add_session_info(pathname,&(node->info));
	if(res<0){
		kfree(node);
		kfree(node_rcu);
		spin_unlock(&sessions_lock);
		return ERR_PTR(res);
	}
	//we need to open the original file always with both read and write permissions.
	flag=(((flags & ~O_RDONLY) & ~O_WRONLY) | O_RDWR);
	fd=open_file(pathname,flag,mode,NO_FD,&file);
	if(fd < 0){
		remove_session_info(&(node->info));
		kfree(node);
		kfree(node_rcu);
		spin_lock(&sessions_lock);
		return ERR_PTR(fd);
	}
	printk(KERN_DEBUG "SessionFS session manager: original file opened successfully, populating session object");
	//we link the session_rcu and the session structs
	node_rcu->session=node;
	//we fill the session object
	INIT_LIST_HEAD(&(node_rcu->list_node));
	node->rcu_node=node_rcu;
	node->file=file;
	node->pathname=pathname;
	rwlock_init(&(node->sess_lock));
	atomic_set(&(node->refcount),1);
	node->incarnations.first=NULL;
	//we flag the session as valid
	node->valid=VALID_NODE;
	printk(KERN_DEBUG "SessionFS session manager: adding session object to the rculist");
	// we insert the new session in the rcu list
	list_add_rcu(&(node_rcu->list_node),&sessions);
	//we release the spinlock
	spin_unlock(&sessions_lock);
	return node;
}

/** \brief Deallocates a `::session_rcu` element
 * \param[in] head The `rcu_head` struct contained inside the `::session_rcu` to deallcoate.
 *
 * __DO NOT__ directly call this function, instead you should let this function be called by `call_rcu()` when nobody is accessing anymore this element.
 */
void delete_session_rcu(struct rcu_head* head){
	struct session_rcu* session_rcu=container_of(head,struct session_rcu,rcu_head);
	printk(KERN_DEBUG "SessionFS session manager: deleting unused session_rcu");
	kfree(session_rcu);
}

/** \brief Copy the contents of a file into another.
 * \param[in] src The source file.
 * \param[in] dst The destination file.
 * \returns 0 on success, an error code on failure.
 *
 *  Reads `::DATA_DIM` bytes from `src` and writes them on `dst`, starting in both files from the beginning and stopping when
 * `src` is has been completely read.
 */
int copy_file(struct file* src,struct file* dst){
	unsigned long long offsetr=0,offsetw=0;
	int read=1,written=1;
	//bytes read, set initially to 1 to make the while start for the first time
	char* data=kzalloc(512*sizeof(char), GFP_USER);
	if(!data){
		return -ENOMEM;
	}
	printk(KERN_DEBUG "SessionFS session manager: starting file copy");
	//we read the file until the read function will not read any more bytes
	while(read>0){
		read=kernel_read(src,data,DATA_DIM,&offsetr);
		if(read<0){
			kfree(data);
			return read;
		}
		//printk(KERN_DEBUG "SessionFS session manager: read %d bytes starting at %lld",read,offsetr);
		written=kernel_write(dst,data,read,&offsetw);
		if(written<0){
			kfree(data);
			return written;
		}
		//printk(KERN_DEBUG "SessionFS session manager: written %d bytes starting at %lld",written,offsetw);
	}
	kfree(data);
	printk(KERN_DEBUG "SessionFS session manager: file copy completed successfully");
	return 0;
}

/** \brief Creates an `::incarnation` and add it to an existing `::session`.
 * \param[in] session The `::session` object that represents the file in which we want to create a new `::incarnation`.
 * \param[in] flags The flags the regulates how the file must be opened.
 * \param[in] pid The pid of the process that wants the create a new `::incarnation`.
 * \param[in] mode The permissions to apply to newly created files.
 * \returns The file descriptor of the new `::incarnation` or an error code (`-EAGAIN` if the parent session is invalid).
 *
 * Creates an `::incarnation` by updateing the information on SysFS using `add_incarnation_info()`, opening a new file,
 * using `open_file()`, copying the contents of the original file in the new file, using `copy_file()`, then creating an
 * `::incarnation` object, filling it with info and adding it to the `incarnations` list of the parent `::session`.
 * The original flags will be modified by adding the `O_CREAT` flag, since the incarnation file must always be created.
 * If the created incarnation is invalid the error code that has invalidated the session can be found in the `::incarnation`
 * `status` parameter.
 * If the incarnation file name has the followinf format: `[original filename]_incarnation_[pid]_[timestamp]` to make
 * the incarantion file unique. If the original pathname is too long we use the following format `/var/tmp/[pid]_[timestamp]`.
 * The timestamp is obtained by calling `ktime_get_real()`.
 *
 * If we have an invalid parent `::session` we return `-EAGAIN`.
 */
struct incarnation* create_incarnation(struct session* session, int flags, pid_t pid, mode_t mode){
	int res=0;
	struct incarnation* incarnation=NULL;
	struct file* file=NULL;
	int fd=NO_FD;
	char *pathname=NULL;

	//we create the pathname for the incarnation
	pathname=kzalloc(PATH_MAX*sizeof(char),GFP_KERNEL);
	if(!pathname){
		return ERR_PTR(-ENOMEM);
	}
	//we create the incarnation object
	incarnation=kzalloc(sizeof(struct incarnation), GFP_KERNEL);
	if(!incarnation){
		kfree(pathname);
		return ERR_PTR(-ENOMEM);
	}
	//if the current session has been detached and it will be freed shortly we abort the incarnation creation
	if(session->valid!=VALID_NODE){
		printk(KERN_INFO "SessionFS session manager: the parent session is invalid, aborting incarnation creation");
		kfree(pathname);
		kfree(incarnation);
		return ERR_PTR(-EAGAIN);
	}
	printk(KERN_DEBUG "SessionFS session manager: allocated necessary memory");
	//we use the actual timestamp so we are resistant to multiple opening of the same session by the same process
	res=snprintf(pathname,PATH_MAX,"%s_incarnation_%d_%lld",session->pathname,pid,ktime_get_real());
	if(res>=PATH_MAX){
		//we make the file shorter by opening it on /var/tmp
		snprintf(pathname,PATH_MAX,"/var/tmp/%d_%lld",pid,ktime_get_real());
	}
	printk(KERN_DEBUG "SessionFS session manager: opening the incarnation file: %s",pathname);
	//we try to open the file
	fd=open_file(pathname,flags | O_CREAT,mode,!NO_FD,&file);
	if(fd<0){
		remove_incarnation_info(&(session->info),&(incarnation->inc_attr));
		kfree(pathname);
		kfree(incarnation);
		return ERR_PTR(fd);
	}
	printk(KERN_DEBUG "SessionFS session manager: adding incarnation info");
	//we add the information on the new incarnation
	res=add_incarnation_info(&(session->info),&(incarnation->inc_attr),pid,fd);
	/** We need to grab the parent `::session` lock in read mode from when we copy the original file over the incarnation file; since we do not
	 * need to protect the lockless list when adding elements, but the `::session` incarnations must be created atomically in respect to close
	 * operations on the same original file
	 * The lock is released when the incarantion has been added to the list. */
	read_lock(&(session->sess_lock));
	if(res==0){
		// if we fail adding info on the incarnation we avoid copying the original file contents in it, since it will be closed shortly after.
		printk(KERN_DEBUG "SessionFS session manager: copying the original file over the incarnation and populating the incarnation object");
		//we disable preemption, since we can't be interrupted while copying the from original file
		///\todo see if they are necessary
		//preempt_disable();
		//smp_mb();
		//we copy the original file in the new incarnation
		res=copy_file(session->file,file);
		///\todo see if they are necessary
		//preempt_enable();
	}
	// we save the result in the status member of the struct, to make the shred library able to tell is the session is valid
	printk(KERN_DEBUG "SessionFS session manager: copy result %d",res);
	incarnation->status=res;
	incarnation->file=file;
	incarnation->pathname=pathname;
	incarnation->filedes=fd;
	incarnation->node.next=NULL;
	incarnation->owner_pid=pid;
	printk(KERN_DEBUG "SessionFS session manager: adding the incarnation to the llist");
	//we add the incarnation to the list of active incarnations
	llist_add(&(incarnation->node),&(session->incarnations));
	//we release the read lock
	read_unlock(&(session->sess_lock));
	return incarnation;
}

/** \brief Removes the given `::incarnation`.
 * \param[in] session The session containing the `::incarnation`n to be removed.
 * \param[in] filedes The file descriptor that identifies the `::incarnation`
 * \param[in] pid The pid of the owner of the `::incarnation`.
 * \param[in] overwrite If set to `::OVERWRITE_ORIG` it will overwrite the original file with the content of the `::incarnation` which is going to be removed, otherwise the current `::incarnation` is simply removed.
 * \returns 0 or an error code.
 *
 * Searches the `incarnations` list of the given `::session` for the current `::incarnation`, copies the contents of the
 * `::incarnation` over the original file (if `overwrite` is set to `::OVERWRITE_ORIG`) and removes it from the list.
 * Then it frees the memory, leaving to the userspace library the task to close and remove the file.
 *
 * When the incarantion is removed, SysFS is updated with `remove_incarnation_info()`.
 *
 * __NOTE__: This method must be protected by obtaining the write lock on the `::incarnation` parent `::session`, otherwise we
 * could mess up the whole list of incarnations for a session.
 */
int delete_incarnation(struct session* session,int filedes, pid_t pid,int overwrite){
	int isfirst=0,res=0;
	//we remove the incarnation from the list of incarnations
	struct llist_node *it=NULL, *first=NULL;
	struct incarnation* incarnation=NULL;
	first=session->incarnations.first;
	printk(KERN_DEBUG "SessionFS session manager: searching for the incarnation to delete");
	//we search for the previous incarnation
	//our incarnation the first in the list?
	incarnation=llist_entry(first,struct incarnation,node);
	printk(KERN_DEBUG "SessionFS session manager: first incarnation: %p",incarnation);
	if(first!=NULL && incarnation->owner_pid==pid && incarnation->filedes==filedes){
		session->incarnations.first=incarnation->node.next;
		isfirst=1;
		printk(KERN_DEBUG "SessionFS session manager: the incarnation to delete is the first in the llist");
	}
	if(!isfirst){
		printk(KERN_DEBUG "SessionFS session manager: navigating the llist to find the incarnation to delete");
		llist_for_each(it,first){
			if(it->next!=NULL){
				//we check if the next element on the list if the incarnation we must find
				incarnation=llist_entry(it->next, struct incarnation, node);
				if(incarnation->owner_pid==pid && incarnation->filedes==filedes){
					printk(KERN_DEBUG "SessionFS session manager: found the incarnation in the list");
					//we eliminate ourselves from the list
					it->next=incarnation->node.next;
					break;
				}
			}
		}
	}
	//we remove the information on the incarnation
	remove_incarnation_info(&(session->info),&(incarnation->inc_attr));
	//we overwrite, if necessary, the content of the original file
	if(overwrite==OVERWRITE_ORIG && incarnation->status == VALID_NODE){
		printk(KERN_DEBUG "SessionFS session manager: copying the content of the incarnation over the original file");
		//before freeing the memory we copy the content of the current incarnation in the original file
		//we disable preemption during the copy operation
		/// \todo check if this is necessary
		//preempt_disable();
		//smp_mb();
		res=copy_file(incarnation->file,session->file);
		/// \todo check if this is necessary
		//preempt_enable();
		if(res<0){
			return res;
		}
	}
	kfree(incarnation->pathname);
	kfree(incarnation);
	printk(KERN_DEBUG "SessionFS session manager: incarnation closed successfully");
	return 0;
}

/** Initializes the `::sessions` global variable as an empty list. Avoiding the RCU initialization since we can't receive
* requests yet, so no one will use this list for now. Then initializes the `::sessions_lock` spinlock.
*/
int init_manager(void){
//we initialize the list normally, since we cannot yet read it.
	INIT_LIST_HEAD(&sessions);
	//now we initialize the spinlock
	spin_lock_init(&sessions_lock);
	return 0;
}

/** To create a new session we check if the original file was already openeded with session semantic, by seraching for an
 * existing `::session` the same `pathname` using `search_session()`.
 * If the found `::session` is invalid or a correspoding `::session` object is not found a new `::session` object will be
 * created, using `init_session()`.
 * Then we create a new `::incarnation` of the original file with `create_incarnation()`.
 *
 * When the `::incarnation` has been created the `refcount` of the parent session is decremented.
 *
 * `-EAGAIN` is returned if the created session is invalid.
 */
struct incarnation* create_session(const char* pathname, int flags, pid_t pid, mode_t mode){
	//we get the first element of the session list
	struct session* session=NULL;
	struct incarnation* incarnation=NULL;
	printk(KERN_DEBUG "SessionFS session manager: searching for an existing session with pathname %s",pathname);
	session=search_session(pathname,NO_FD,NO_PID);
	/*session_it now is either null or contains the element which represents the session for the file in pathname,
	 * however if the session is invalid we need to create another valid session object */
	if(session==NULL || session->valid!=VALID_NODE){
		if(session!=NULL && session->valid!=VALID_NODE){
			atomic_sub(1,&(session->refcount));
			printk(KERN_DEBUG "SessionFS session manager: the found session has become invalid, trying deallocation");
			delete_session(session);
		}
		printk(KERN_DEBUG "SessionFS session manager: session object not found, creating a new session with pathname %s",pathname);
	//we create the session object if necessary
		session=init_session(pathname, flags,mode);
		if(IS_ERR(session)){
			//we return the error code (as an incarnation*)
			return (struct incarnation*)session;
		}
	}
	//we create the file incarnation
	printk(KERN_DEBUG "SessionFS session manager: adding a new incarnation to session object %s",pathname);
	incarnation=create_incarnation(session,flags,pid,mode);
	atomic_sub(1,&(session->refcount));
	//we deallcate the session if it has become invalid during creation
	if(PTR_ERR(incarnation)==-EAGAIN){
		delete_session(session);
	}
	printk(KERN_DEBUG "SessionFS session manager: incarnation created, check the incarnation status to see if it is valid");
	return incarnation;
}

/**
 * This function will close one session, by finding the corresponding `::incarnation`, using `search_session()`,
 * copying the incarnation file over the original file (atomically in respect to other session operations
 * on the same original file, and only if the `::session` is valid), and deleting the incarnation, using `delete_incarnation()`.
 * To do so we grab the `sess_lock` in write mode when closing the `::incarantion` and we release it aftwards.
 * If after the incarnation deletion the `::session` has no other `::incarnation`(s) the it will also schedule the `::session` to
 * be removed.
 *
 * If we cannot locate the parent session of an incarnation `-EBADF` is returned.
 */
int  close_session(const char* pathname,int fdes, pid_t pid){
	//we locate the session in which we need to remove an incarnation
	int res=0, commit=OVERWRITE_ORIG;
	struct session* session=NULL;
	printk(KERN_DEBUG "SessionFS session manager: searching for the incarnation to remove");
	session=search_session(pathname,fdes,pid);
	if(session==NULL){
		printk(KERN_DEBUG "SessionFS session manager: session not found, aborting");
		return -EBADF;
	}
	//we get the write lock on the session
	write_lock(&session->sess_lock);
	//If the session if still valid we overwrite the original file, otherwise we simply delete the `::incarantion`.
	if(session->valid!=VALID_NODE){
		printk(KERN_DEBUG "SessionFS session manager: invalid session, the original file will not be overwritten");
		commit=!OVERWRITE_ORIG;
	}
	//we eliminate the incarnation and we overwrite the original file with the incarnation content.
	res=delete_incarnation(session, fdes, pid,commit);
	if(res<0){
		return res;
	}
	printk(KERN_DEBUG "SessionFS session manager: elimination of the incarnation successful");
	/**
	 * To remove a session object we need to check several conditions:
	 * - The `::session` must be not in use by other threads (refcount==1)
	 * - The `::incarnation` list must be empty
	 * - The `::session` must be still valid and not already marked for deletion
	 */
	if(atomic_read(&(session->refcount))==1 && llist_empty(&(session->incarnations)) && session->valid==VALID_NODE){
		printk(KERN_DEBUG "SessionFS session manager: detected empty llist for the associated session, attempting to purge the session object");
		///If the current `::session` muset be removed, we flag it as invalid, to avoid having new incarnations created in here before deallocating it and making sure that it will be eventually deallocated.
		session->valid=!VALID_NODE;
		//we get the spinlock over the session list, to avoid running concurrently with another list modification primitive
		spin_lock(&sessions_lock);
		///Then, we can remove the current `::session` object from the rcu list, using the `::sessions_lock` spinlock to avoid concurrent operations.
		printk(KERN_DEBUG "SessionFS session manager: removing the element from the rcu_list");
		list_del_rcu(&(session->rcu_node->list_node));
		//we release the spinlock
		spin_unlock(&sessions_lock);
		//we register a callback to free the memory associated to the session
		printk(KERN_DEBUG "SessionFS session manager: registering callback to deallocate the session_rcu object");
		call_rcu(&(session->rcu_node->rcu_head),delete_session_rcu);
		//we try to delete the session item
	}
	//we release the lock
	write_unlock(&(session->sess_lock));
	//we decrement the refcount
	atomic_sub(1,&(session->refcount));
	///Finally, we try to deallocate the `::session` if is invalid, using `delete_session()`.
	if(session->valid!=VALID_NODE){
		delete_session(session);
	}
	return 0;
}

/**
 * This method will walk through the `::sessions` list and each `::incarnation` list, deleting all the
 * `::incarnation`(s) and `::session`(s) in which the process that has requested it is not active anymore, leaving the original files untouched.
 * If there is an `::incarnation` that is still in use by an active process it will be preserved.
 *
 * To check if an `::incarnation` is still active we get the struct pid from the pid number included in the `::incarnation`;
 * if we receive it, the process is at most in a zombie state.
 *
 * When walking the `::sessions` list we increment the refcount of the `::session` in use to avoid having it removed while we are using it.
 * When we traverse the `::session` `incarnations` list we grab the `sess_lock` in write mode.
 * To remove incarnation we remove all the elements in the `incarnations` list and we add back the objects associated with an active pid,
 * closing the others without modifing the original file and updating SysFS with `remove_incarnation_info()`.
 * If a `::session` has an empty `incarnations` list it will be flagged as invalid and removed during a second walk, where,
 * with `delete_incarnation()`, we remove all the invalid `::session`(s)..
 *
 * __NOTE:__ For dead incarnations that have not been closed we leave the files in the folder, since can't remove them from kernel space.
 * You will need to manually remove incarnations file for invalid processes.
 */
int clean_manager(void){
	int manager_status=MANAGER_EMPTY,active_sessions=0,dead_sessions=0;
	struct pid* pid;
	struct session_rcu* session_rcu=NULL;
	struct incarnation* incarnation=NULL;
	struct llist_node *llist_it=NULL,*llist_node=NULL,*llist_tmp;
	struct task_struct* task;
	//we take the spinlock to be sure to be the last to have accessed this list
	if(list_first_or_null_rcu(&sessions,struct session_rcu,list_node)!=NULL){
		rcu_read_lock();
				printk(KERN_DEBUG "SessionFS session manager: we have elements in the rcu list, checking sessions");
		list_for_each_entry_rcu(session_rcu,&sessions,list_node){
			//we skip invalid session that will be deleted shortly
			if(session_rcu->session->valid==VALID_NODE){
				atomic_add(1,&(session_rcu->session->refcount));
				//we check the remaining sessions incarnations
				if(!llist_empty(&(session_rcu->session->incarnations))){
					// we remove all the elements from the llist
					write_lock(&(session_rcu->session->sess_lock));
					llist_node=llist_del_all(&(session_rcu->session->incarnations));
					//we walk the deleted llist searching for incarnations in use by a valid process
					incarnation=NULL;
					llist_for_each_safe(llist_it,llist_tmp,llist_node){
						//if the incarnation variable is not null we need to deallocate the previous incarnation struct since it is invalid.
						if(incarnation!=NULL){
							kfree(incarnation);
							incarnation=NULL;
						}
						incarnation=llist_entry(llist_it,struct incarnation,node);
						//we try to get the pid struct for the pid in the
						pid=find_get_pid(incarnation->owner_pid);
						task=get_pid_task(pid,PIDTYPE_PID);
						if(pid==NULL || task_is_stopped_or_traced(task)){
							dead_sessions++;
							//if we couldn't get the pid struct or if is NULL we consider the process is dead.
							//we don't close the file associated to the incarnation since the kernel already closes it and we could get a GPF.
							printk(KERN_DEBUG "SessionFS session manager: %s is owned by a dead process, freeing the session",incarnation->pathname);
							remove_incarnation_info(&(session_rcu->session->info),&(incarnation->inc_attr));
							kfree(incarnation->pathname);
							//we will deallcate the incarnation at the beginning of the next iteration if the next item if not NULL
							//we could mess un the llist_it if deallcate it now (we get a NULL pointer dereference when we advance in the loop)
						} else{
							if(IS_ERR(pid)){
								printk(KERN_WARNING "SessionFS session manager: error while getting pid struct for %d",incarnation->owner_pid);
							}
							//we increment the active sessions count
							active_sessions++;
							//we note that the manager contains active sessions
							manager_status=!MANAGER_EMPTY;
							// we put back the incarnation in the llist
							incarnation->node.next=NULL;
							llist_add(&(incarnation->node),&(session_rcu->session->incarnations));
							//no deallocating an active incarnation without having closed it!
							incarnation=NULL;
						}
					}
					write_unlock(&(session_rcu->session->sess_lock));
					if(incarnation!=NULL){
						kfree(incarnation);
						incarnation=NULL;
					}
				}
				atomic_sub(1,&(session_rcu->session->refcount));
				if(atomic_read(&(session_rcu->session->refcount))==0 && llist_empty(&(session_rcu->session->incarnations))){
					//we mark the session as invalid for future deletion
					session_rcu->session->valid=!VALID_NODE;
				}
			}
		}
		rcu_read_unlock();
		//if we have found some dead incarnations we need to check if their session object can be deallocated
		if(dead_sessions>0){
			printk(KERN_DEBUG "SessionFS session manager: checking for invalid session objects");
			spin_lock(&sessions_lock);
			list_for_each_entry_rcu(session_rcu,&sessions,list_node){
				if(session_rcu->session->valid!=VALID_NODE){
					//we can remove the current session object from the rcu list
					list_del_rcu(&(session_rcu->list_node));
					//we register a callback to free the memory associated to the session
					call_rcu(&(session_rcu->rcu_head),delete_session_rcu);
					delete_session(session_rcu->session);
				}
			}
			spin_unlock(&sessions_lock);
		}
	}
	if(manager_status==MANAGER_EMPTY){
		printk(KERN_DEBUG "SessionFS session manager: session list empty, session manager can be released");
	} else {
		printk(KERN_DEBUG "SessionFS session manager: session list contains active sessions, session manager can't be released.");
	}
	printk(KERN_DEBUG "SessionFS session manger: valid session num: %d, dead sessions: %d",active_sessions,dead_sessions);
	return active_sessions;
}
