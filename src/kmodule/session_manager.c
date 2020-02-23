/** \file session_manager.c
 * \brief Implementation of the session manager.
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

///The structs that represent sessions, incarnation and their informations.
#include "session_types.h"

///The module that handles session information
#include "session_info.h"

#include "session_manager.h"

/// Used to toggle the necessity of a file descriptor in ::open_file.
#define NO_FD 0

///Permissions to be given to the newly created files.
#define DEFAULT_PERM 0644

///Used to determine if a session node is valid.
#define VALID_NODE 0

///The portion of the file which is copied at each read/write iteration
#define DATA_DIM 512

///Used to determine if the content of the incarnation must overwrite the original file on close
#define OVERWRITE_ORIG 0

///List of the active sessions.
struct list_head sessions;

///Spinlock used to update the list of the active sessions
spinlock_t sessions_lock;

/** \brief Opens a file from kernel space.
 * \param[in] pathanme String that represents the file location and name **must be in kernel memory**
 * \param[in] flags Flags that will regulate the permissions on the file.
 * \param[in] fd_needed If set to ::NO_FD the file descriptor for the openend file will not be designated (used only when we are opening original files).
 * \param[out] file pointer to the file struct of the opened file.
 * \returns The file descriptor on success, the error code or ::NO_FD if no file descriptor was requested.
 *
 * This function will open the file at the specifid pathname and then associate a file descriptor to the opened file.
 * If the `O_CREAT` flags is specified the permissions for the newly created file will be ::DEFAULT_PERM.
 * If ::fd_needed is set to ::NO_FD then the opened file won't have a file descriptor associated.
 */
int open_file(const char* pathname, int flags,int fd_needed, struct file** file){
	struct file* f=NULL;
	int fd=-ENOENT;
	//we try to open the file
	file=filp_open(pathname,flags,DEFAULT_PERM);
	if (IS_ERR(filp)) {
		fd = PTR_ERR(filp);
	} else {
		if(fd_needed){
			//find a new file descriptor
			fd=get_unused_fd_flags(flags);
			if(fd>=0){
				//notify processes that a file has been opened
				fsnotify_open(f);
				//register the descriptor in the intermediate table
				fd_install(fd, f);
			}
		} else {
			fd=NO_FD;
		}
	}
	*file=f;
	return fd;
}
/** \brief Searches for a session with a given pathname.
 * \param pathname The pathname that identifies the session.
 * \param filedes The file descriptor of an incarantion.
 * \param pid The pid of the process that owns the incarnation.
 * \returns A pointer to the found session object or NULL.
 * Searches by navigating the rcu list a session which matches the given ::pathname.
 * If pathname is NULL and filedes is differrent from ::NO_FD then it searches for the session that contains
 */
struct session* search_session(const char* pathname,int filedes, pid_t pid){
	//paramters check
	if(pathname==NULL && filedes==NO_FD){
		return NULL;
	}
	struct incarnation *inc_it,*inc_tmp;
	//we get the first element of the session list
	struct session *session_it=NULL, *found=NULL;
	//we get the read lock on the rcu
	rcu_read_lock();
	session_it= list_first_or_null_rcu(&sessions,struct session,list_node);
	//we need to walk all the list to see if we have alredy other sessions opened for the same pathname
	list_for_each_entry_rcu(session_it,&sessions,list_node,NULL){
		if(session_it != NULL && strcmp(session_it->pathname,pathanme) == 0){
			found=session_it;
			if(filedes > NO_FD){
				/// \todo verify if we can traverse the list without removing all the entries
				llist_for_each_entry_safe(inc_it,inc_tmp,found->incarnations->first,next){
					if(inc_it->owner_pid==pid && inc_it->filedes==filedes){
						rcu_read_unlock();
						return found;
					}
				}
			} else {
				rcu_read_unlock();
				return found;
			}
		}
	}
	rcu_read_unlock();
	return found;
}

/**
 * \brief Initializes the session information for the given pathname.
 * \param[in] pathname The path of the original file.
 * \param[in] flags The flags that regulate the access to the original file.
 * Opens the file, then creates the session object and fills it with the session
 * Session information is initialized by creating a session object, filling it with the necessary info, opening the file
 * and adding the object to the session list.
 * The original flags will be modified by removing the `O_RDONLY` and `O_WRONLY` in favor of `O_RDWR`, since we will always
 * read and write on this file.
 * With the specified flags if we preserve the semantic of the `O_EXCL` flag.
 */
struct session* init_session(const char* pathname,int flags){
	//we try to open the original file with the given flags
	int fd=-ENOENT;
	int res=0;
	int flag;
	struct session* node=NULL;
	//we get the spinlock to avoid race conditions while creating the session object
	spin_lock(sessions_lock);
	//we check if, while we were searching, the session has been already created
	node=search_session(pathname);
	if(node!=NULL){
		//we return the found session;
		spin_unlock(sessions_lock);
		return node;
	}
	//we allocate the new session object
	node=kmalloc(sizeof(struct session),GPF_KERNEL);
	if(!node){
		spin_unlock(sessions_lock);
		return ERR_PTR(-ENOMEM);
	}
	//we allocate memory for the original file pathname
	node->pathname=kzalloc(sizeof(char)*PATH_MAX, GFP_KERNEL);
	if(!node->pathname){
		spin_unlock(sessions_lock);
		kfree(node);
		return ERR_PTR(-ENOMEM);
	}
	//we copy the pathname into the session object
	strncpy(node->pathname,pathname,sizeof(char)*PATH_MAX);
	//we need to open the original file always with both read and write permissions.
	flag=flags & ~O_RDONLY & ~O_WRONLY | O_RDWR;
	struct file** file;
	fd=open_file(pathname,flag,NO_FD,file);
	if(fd < 0){
		kfree(node->pathname);
		kfree(node);
		spin_unlock(sessions_lock);
		return ERR_PTR(fd);
	}
	//we fill the session object
	INIT_LIST_HEAD(&(node->list_node));
	node->file=*file;
	node->pathname=pathname;
	rwlock_init(node->sess_lock);
	node->incarnations=LLIST_HEAD_INIT(incarnations);
	//we flag the session as valid
	node->valid=VALID_NODE;
	//we update the info on the device kobject
	res=add_session_info(node->pathname,&(node->info));
	if(res<0 || res==NULL){
		kfree(node->pathname);
		kfree(node);
		spin_unlock(sessions_lock);
		if(res<0){
			return res;
		}
		return -ENOMEM;
	}
	// we insert the new session in the rcu list
	rcu_list_add(node,sessions);
	//we release the spinlock
	spin_unlock(sessions_lock);
	return node;
}
/** \brief Deallocates the given session object.
 * \param[in] session The session object to deallocate.
 * This function is registered as a callback after `list_del_rcu`, to free the memory used by teh session when nobody is accessing it.
 * **DO NOT** call this function directly, unless you are shure that you won't have race condition on the rculist.
 */
void delete_session(struct session* session){
	//before deleting the session we grab the write lock to make sure that we don't deallocate the session while someone is reading it
	write_lock(session->sess_lock);
	remove_session_info(&(session->sess_info));
	filp_close(session->file,NULL);
	/// \todo TODO check if we need to deallocate the struct file for the original file.
	kfree(session->pathname);
	session->file=NULL;
	session->pathname=NULL;
	write_unlock(sess_lock);
	kfree(session);
}

/** \brief Copy the contents of a file into another.
 * \param[in] src The source file.
 * \param[in] dst The destination file.
 * \returns 0 on success, an error code on failure.
 */
int copy_file(struct file* src,struct file* dst){
	int read,written,offset;
	//bytes read, set initially to 1 to make the while start for the first time
	read=1;
	written=0;
	offset=0;
	char* data=kzalloc(512*sizeof(char), GFP_USER);
	//we read the file until the read function will not read any more bytes
	while(read>0){
		read=vfs_read(src,data,DATA_DIM,offset);
		if(read<0){
			kfree(data);
			return read;
		}
		written=vfs_write(dst,data,DATA_DIM,offset);
		if(written<0){
			kfree(data);
			return written;
		}
		offset+=written;
	}
	kfree(data);
	return 0;
}

/** \brief Creates an incarnation and add it to an existing session.
 * \param[in] session The session object that represents the file in which we want to create a new incarnation.
 * \param[in] flags The flags the regulates how the file must be opened.
 * \param[in] pid The pid of the process that wants the create a new incarnation.
 * \returns The file descriptor of the new incarnation or an error code.
 *
 * Creates an incarnation by opening a new file, copying the contents of the original file in the new file, then
 * creating an incarnation object, filling it with info and adding it to the list of incarnations.
 * The original flags will be modified by adding the `O_CREAT` flag, since the incarnation file must always be created.
 * If the created incarnation is invalid the error code that has invalidated the session can be found in the incarnation::status parameter.
 */
struct incarnation* create_incarnation(struct session* session, int flags, pid_t pid){
	/* we get the read lock since we do not need to protect the lockless list when adding elements, but the session
	* incarnations must be created atomically in respect to close operations on the same original file
	*/
	read_lock(session->sess_lock);
	//if the current session has been detached and it will be freed shortly we abort the incarnation creation
	if(session->valid!=VALID_NODE){
		read_unlock(session->sess_lock);
		return NULL;
	}
	//we create the pathname for the incarnation
	int res=0;
	char *pathname=kzalloc(PATH_MAX*sizeof(char),GPF_KERNEL);
	if(!pathname){
		read_unlock(session->sess_lock);
		return -ENOMEM;
	}
	//we use the actual timestamp so we are resistant to multiple opening of the same session by the same process
	res=snprintf(pathname,PATH_MAX,"%s_%d_%d_incarnation",session->pathname,pid,ktime_get_real());
	if(res>=PATH_MAX){
		//we make the file shorter by opening it on /var/tmp
		snprintf(pathname,PATH_MAX,"/var/tmp/%d_%d",pid,ktime_get_real());
	}
	//we create the incarnation object
	struct incarnation* incarnation;
	incarnation=kmalloc(sizeof(struct incarnation), GFP_KERNEL);
	if(!incarnation){
		read_unlock(session->sess_lock);
		kfree(pathname);
		return -ENOMEM;
	}
	//we add the information on the new incarnation
	res=add_incarnation_info(&(session->sess_info),&(incarnation->inc_attr),pid);
	if(res<0){
		remove_incarnation_info(session->sess_info,&(incarnation->inc_attr));
		read_unlock(session->sess_lock);
		kfree(pathname);
		kfree(incarnation);
		return res;
	}
	//we try to open the file
	int fd;
	struct file** file;
	fd=open_file(pathname,flags | O_CREAT,!NO_FD,file);
	if(fd<0){
		read_unlock(session->sess_lock);
		kfree(pathname);
		kfree(incarnation);
		return ERR_PTR(fd);
	}
	//we copy the original file in the new incarnation
	res=copy_file(session->file,file);
	// we save the copy result in the status member of the struct
	incarnation->status=res;
	incarnation->session_info=session;
	incarnation->file=file;
	incarnation->pathname=pathname;
	incarnation->filedes=fd;
	incarnation->flags=flags;
	incarnation->owner_pid=pid;
	//we add it to the list
	//we add the incarnation to the list of active incarnations
	llist_add(incarnation->next,session->incarnations);
	//we release the read lock
	read_unlock(session->sess_lock);
	return incarnation;
}

/** \brief Removes the given incarnation.
 * \param[in] session The session containing the incarnation to be removed.
 * \param[in] filedes The file descriptor that identifies the incarnation
 * \param[in] pid The pid of the owner of the incarnation.
 * \param[in] force If set to ::OVERWRITE_ORIG it will overwrite the original file with the content of the incarnation which is going to be removed, otherwise the current incarnation is simply removed.
 * \param[out] pathname The pathname to the incarnation which must be closed and removed by the shared library.
 * Searches the incarnation list for the current incarnation, copies the contents of the incarnation over the original file (if ::force is not set) and removes it from the list.
 * Then it frees the memory,leaving to the userspace library the task to close and remove the file.
 *
 * **NOTE**: This method must be protected by obtaining the write lock on the ::incarnation parent ::session, otherwise we
 * could mess up the whole list of incarnations for a session.
 */
int delete_incarnation(struct session* session,int filedes, pid_t pid,int overwrite,const char** pathname){
	int isfirst=0,res;
	//we remove the incarnation from the list of incarnations
	struct llist_node *it, *first;
	struct incarnation* incarnation;
	first=session->incarnations.first;
	//we search for the previous incarnation
	//if our incarnation the first in the list?
	if(first!=NULL && first->owner_pid==pid && first->filedes==filedes){
		incarnation=llist_entry(it->next, struct incarnation, next);
		sessions->incarnations.first=incarnation->next;
		isfirst=1;

	}
	if(!isfirst){
		llist_for_each(it,first){
			if(it->next!=NULL){
				//we check if the next element on the list if the incarnation we must find
				incarnation=llist_entry(it->next, struct incarnation, next);
				if(incarnation->owner_pid==pid && incarnation->filedes==filedes){
					//we eliminate ourselves from the list
					it->next=incarnation->next;
					break;
				}
			}
		}
	}
	//we remove the information on the incarnation
	remove_incarnation_info(&(session->sess_info),&(incarnation->inc_attr));
	/// \todo TODO: check if the overwrite param is really necessary
	if(overwrite==OVERWRITE_ORIG && incarnation->status == VALID_NODE){
		//before freeing the memory we copy the content of the current incarnation in the original file
		res=copy_file(incarnation->file,session->file);
		if(res<0){
			return res;
		}
	}
	*pathname=incarnation->pathname;
	//we don't free the memory for the struct file and the pathname since we will leave to the library the task to close the file descriptor and remove the file
	kfree(incarnation);
	return 0;
}

/** Initializes the ::sessions global variable as an empty list. Avoiding the RCU initialization since we can't receive
* requests yet, so no one will use this list for now. Then initializes the ::sessions_lock spinlock.
*/
int init_manager(void){
//we initialize the list normally, since we cannot yet read it.
	INIT_LIST_HEAD(sessions);
	//now we initialize the spinlock
	spin_lock_init(sessions_lock);
	return 0;
}

/** To create a new session we check if there original file was already openeded with session semantic, if necessary we
 * open it creating the correspoding session object, and then we create a new incarnation of the original file.
 */
struct incarnation* create_session(const char* pathname, int flags, pid_t pid){
	int res=0;
	//we get the first element of the session list
	struct session* session=NULL;
	session=search_session(pathname);
	//session_it now is either null or contains the element which represents the session for the file in pathname
	if(session==NULL){
	//we create the session object if necessary
		session_it=init_session(pathname, flags);
		if(IS_ERR(session)){
			return session;
		}
	}
	//we create the file incarnation
	struct incarnation* incarnation;
	incarnation=create_incarnation(session,flags,pid);
	return incarnation;
}

/** This function will close one session, by finding the incarnation (from the original file pathname, owner pid
 * and file descriptor), copying the incarnation over the original file (atomically in respect to other session operations on the same original file) and delete the incarnation.
 * If after the incarnation deletion the session has no other incarnation the it will also schedule the session to
 * be destroyed.
 */
int  close_session(int fdes, pid_t pid, const char** incarnation_pathname){
	//we locate the session in which we need to remove an incarnation
	int res=0;
	struct session* session=NULL;
	search_session(NULL,fdes,pid);
	if(session==NULL){
		return -EBADF;
	}
	//we get the write lock on the session
	write_lock(session->sess_lock);
	//we check if the session if still valid
	if(session->valid!=VALID_NODE){
		return -EBADF;
	}
	//we eliminate the incarnation and we overwrite the original file with the incarnation content.
	res=delete_incarnation(session, fdes, pid,OVERWRITE_ORIG,incarnation_pathname);
	write_unlock(session->sess_lock);
	//we check if the list of incarnations is empty
	if(llist_empty(session->incarnations)){
		//we get the spinlock over the session list, to avoid runing concurrently with another list modification primitive
		spin_lock(sessions_lock);
		//we can remove the current session object from the rcu list
		list_del_rcu(&(session->list_node));
		//we register a callback to free the memory associated to the session
		call_rcu(session->rcu_head,delete_session(session));
		//we release the spinlock
		spin_unlock(sessions_lock);
	}
	if(res<0){
		//we report that the original file is now invalid, since we have failed in the copy
		return res;
	}
	return 0;
}

/**
 * **DO NOT** call this method BEFORE having unregistered the device since it will mess up the rculist and each incarnation list.
 *
 * This method will walk through the rcu list and each incarnation list, deleting all the
 * incarnations and sessions, leaving the original files untouched.
 *
 * **NOTE**: For incarnations that have not been closed we leave the files in the folder, since can't remove them from kernel space.
 */
void release_manager(void){
	struct session* session=NULL;
	struct incarnation* incarnation=NULL;
	struct llist_node* llist_node=NULL;
	struct list_head* session_it==NULL;
	//we take the spinlock to be sure to be the last to have accessed this list
	spin_lock(sessions_lock);
	session_it=sessions->next;
	/// \todo TODO check if we need to be protected against concurrent rcu reads
	while(session_it != NULL){
		session=list_entry(session_it,struct session,list_node);
		session_it=session_it->next;
		//we eliminate the remaining sessions incarnations
		if(session != NULL && session->incarnations.first != NULL){
			//we take the write lock to be sure to be the last to have accessed the incarnation list
			write_lock(session->sess_lock);
			llist_node=session->incarnations.first;
			while(llist_node != NULL){
				incarnation=llist_entry(llist_node,struct incarnation,next);
				llist_node=llist_node->next;
				/// \todo TODO remove the incarnation file descriptor
				filp_close(incarnation->file);
				kfree(pathname);
				kfree(incarnation);
			}
			write_unlock(session->sess_lock);
		}
		delete_session(session);
	}
	INIT_LIST_HEAD(sessions);
	spin_unlock(sessions_lock);
}
