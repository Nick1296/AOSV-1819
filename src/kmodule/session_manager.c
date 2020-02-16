/** \file
 * \brief Implementation of the session manager.
 * This file contains the implementation of the session manager, which will handle the creation and deletion of a session
 * keeping track of the opened sessions for each file.
 */

// for the list_head struct and list api
#include <linux/types.h>
// for PATH_MAX, which is currently unused since we (TODO) want to copy the path from user memory into kernel memory
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
#include <uapi/asm-generic/errno-base.h>
//for timestamps
#include <linux/timekeeping.h>
//for the O_* flags
#include <uapi/asm-generic/fcntl.h>

#include "session_manager.h"

/** \struct
 * \brief Informations on an incarnation of a file.
 * \param incarnation_next Next incarnation on the list.
 * \param filedes File Descriptor of the incarnation.
 * \param owner_pid Pid of the process that has requested the incarnation.
 */
struct incarnation{
	struct llist_node* next;
	struct file* file;
	const char* pathname;
	int filedes;
	pid_t owner_pid;
};


/** \struct
 * \brief General information on a session.
 * \param pathame Pathname of the file that is opened with session semantic.
 * \param filedes Descriptor of the file opened with session semantic.
 * \param sess_lock read-write lock used to access ensure serialization in the session closures.
 * \param incarnations List (lockless) of the active incarnations of the file.
 * \param list_node used to navigate the list of sessions.
 */
struct session{
	struct list_head list_node;
	struct file* file;
	char* pathname;
	int filedes;
	int flags;
	rwlock_t sess_lock;
	struct llist_head incarnations;
};


///List of the active sessions.
struct list_head sessions;

///Spinlock used to update the list of the acive sessions
spinlock_t sessions_lock;

/** \brief Opens a file from kernel space.
 * \param[in] pathanme String that represents the file location and name **must be in kernel memory**
 * \param[in] flags Flags that will regulate the permissions on the file.
 * \param[out] file pointer to the file struct of the opened file.
 * \returns The file descriptor on success or the error code.
 *
 * This function will open the file at the specifid pathname and then associate a file descriptor to the opened file.
 * If the `O_CREAT` flags is specified the permissions for the newly created file will be 0644.
 */
int open_file(const char* pathname, int flags, struct file** file){
	struct file* f;
	int fd=-ENOENT;
	//we try to open the file
	file=filp_open(pathname,flags,0644);
	if (IS_ERR(filp)) {
		fd = PTR_ERR(filp);
	} else {
	//find a new file descriptor
		fd=get_unused_fd_flags(flags);
		if(fd>=0){
			//notify processes that a file has been opened
			fsnotify_open(f);
			//register the descriptor in the intermediate table
			fd_install(fd, f);
		}
	}
	f=&file;
	return fd;
}

/**
 * \brief Initializes the session informations for the given pathname.
 * \param[in] pathname The path of the original file.
 * \param[in] flags The flags that regulate the access to the original file.
 * Opens the file, then creates the session object and fills it with the session
 * Session information is initialized by creating a session object, filling it with the necessary info, opening the file
 * and adding the object to the session list.
 * The original flags will be modified by removing the `O_RDONLY` and `O_WRONLY` in favor of `O_RDWR`, since we will always
 * read and write on this file.
 */
struct session* init_session(const char* pathname,int flags){
	//we try to open the original file with the given flags
	int fd=-ENOENT;
	int res=0;
	int flag;
	//we need to open the original file always with both read and write permissions.
	flag=flags & !O_RDONLY & !O_WRONLY | O_RDWR;
	struct file** file;
	fd=open_file(pathname,flag,file);
	if(fd < 0){
		return ERR_PTR(fd);
	}
	//we allocate the new session object
	struct session* node;
	node=kmalloc(sizeof(struct session),GPF_KERNEL);
	if(!node){
		return ERR_PTR(-ENOMEM);
	}
	node->file=*file;
	node->pathname=pathname;
	node->filedes=fd;
	rwlock_init(node->sess_lock);
	node->incarnations=LLIST_HEAD_INIT(incarnations);
	//we get the spinlock
	spin_lock(sessions_lock);
	// we insert the new session in the rcu list
	rcu_list_add(node,sessions);
	//we release the spinlock
	spin_unlock(sessions_lock);
	return node;
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
		read=vfs_read(src,data,512,offset);
		if(read<0){
			return read;
		}
		written=vfs_write(dst,data,512,offset);
		if(written<0){
			return written;
		}
		offset+=written;
	}
	return 0;
}
/**
 */
int close_file(struct file* file, int filedes){

}

/** \brief Creates an incarnation and add it to an existing session.
 * \param[in] session The session object that represents the file in which we want to create a new incarnation.
 * \param[in] flags The flags the regulates how the file must be opened.
 * \param[in] pid The pid of the process that wants the create a new incarnation.
 * \returns The file descriptor of the new incarnation or an error code.
 * Creates an incarnation by opening a new file, copying the contents of the original file in the new file, then
 * creating an incarnation object, filling it with info and adding it to the list of incarnations.
 * The original flags will be modified by adding the `O_CREAT` flag, since the incarnation file must always be created.
 */
int create_incarnation(struct session session, int flags,pid_t pid){
	//we create the pathname for the incaration
	int res=0;
	char *pathname=kzalloc(PATH_MAX*sizeof(char),GPF_KERNEL);
	if(!pathname){
		return -ENOMEM;
	}
	//we use the actual timestamp so we are resistant to multiple opening of the same session by the same process
	res=snprintf(pathname,PATH_MAX,"%s_%d_%d_incarnation",session->pathname,pid,ktime_get_real());
	if(res>=PATH_MAX){
		//we make the file shorter by opening it on /var/tmp
		snprintf(pathname,PATH_MAX,"/var/tmp/%d_%d",pid,ktime_get_real());
	}
	//we try to open the file
	int fd;
	struct file** file;
	fd=open_file(pathname,flags | O_CREAT,file);
	if(fd<0){
		kfree(pathname);
		return fd;
	}
	//we copy the orignal file in the new incarnation
	res=copy_file(session->file,file);
	if(res<0){
		kfree(pathname)
		close_file(); /// \todo close file and remove file descriptor
		return res;
	}
	//we create the incarnation object
	struct incarnation* incarnation;
	incarnation=kmalloc(sizeof(struct incarnation), GFP_KERNEL);
	incarnation->file=file;
	incarnation->pathname=pathname;
	incarnation->filedes=fd;
	incarnation->flags=flags;
	incarnation->owner_pid=pid;
	//we add it to the list
	//we get the read lock since we do not need to protect the lockless list when adding elements
	read_lock(session->sess_lock);
	//we add the incarnation to the list of active incarations
	llist_add(incarnation->next,session->incarnations);
	//we release the read lock
	read_unlock(session->sess_lock);
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

/** To create a new sesssion we check if there origianl file was already openeded with session semantic, if necessary we
 * open it creating the correspoding session object, and then we create a new incarnation of the original file.
 */
int create_session(const char* pathname, int flags, pid_t pid){
	int res=0;
	//we get the first element of the sessiion list
	struct session* session_it;
	//we get the read lock on the rcu
	rcu_read_lock();
	session_it= list_first_or_null_rcu(&sessions,struct session,list_node);
	//we need to walk all the list to see if we have alredy other sessions opened for the same pathname
	while(session_it != NULL && strcmp(session_it->pathname,pathanme) != 0){
		session_it= list_next_or_null_rcu(&sessions, session_it->list_node, struct session, list_node)
	}
	//we release the read lock on the rcu
	rcu_read_unlock();
	//session_it now is either null or contains the element which represents the session for the file in pathname
	if(!session_it){
	//we create the session object if necessary
		session_it=init_session(pathname, flags);
		if(IS_ERR(session_it)){
			return PTR_ERR(session_it);
		}
	}
	//we create the file incarnation
	res=create_incarnation(session_it,flags,pid);
	if(res<0){
		///\todo delete session in case of error
		delete_session();
	}
	return res;
}
