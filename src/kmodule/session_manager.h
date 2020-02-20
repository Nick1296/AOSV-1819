/** \file session_manager.h
 * \brief APIs used to interact with the session manger.
 * This file contains the APIs of the session manager, which are used to initialize and release the manager and to add
 * and remove sessions.
 */
#ifndef SESS_MAN_H
#define SESS_MAN_H

#include <linux/types.h>

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

/** \struct incarnation
 * \brief Informations on an incarnation of a file.
 * \param next Next incarnation on the list.
 * \param file The struct file that represents the incarantion file.
 * \param pathname The pathanme of the incarnation file.
 * \param filedes File descriptor of the incarnation.
 * \param owner_pid Pid of the process that has requested the incarnation.
 * \param status Contains the error code that could have invalidated the session. If its value is less than 0 then the incarnation is invalid and must be closed as soon as possible.
 */
struct incarnation{
	struct llist_node* next;
	struct file* file;
	const char* pathname;
	int filedes;
	pid_t owner_pid;
	int status;
};


/** \struct session
 * \brief General information on a session.
 * \param list_node used to navigate the list of sessions.
 * \param rcu_head The rcu head structure used to protect the list with RCU.
 * \param incarnations List (lockless) of the active incarnations of the file.
 * \param file The struct file that represents the original file.
 * \param pathame Pathname of the file that is opened with session semantic.
 * \param sess_lock read-write lock used to access ensure serialization in the session closures.
 * \param filedes Descriptor of the file opened with session semantic.
 * \param valid This parameter is used (after having gained the rwlock) to check if the session object is still attached to the rculist.
 * If the session object has been removed from the rculist the value of this parameter will be different from ::VALID_NODE.
 */
struct session{
	struct list_head list_node;
	struct rcu_head rcu_head;
	struct llist_head incarnations;
	struct file* file;
	const char* pathname;
	rwlock_t sess_lock;
	int valid;
};

/** \brief Initialization of the session manager data structures
 * \returns 0 on success or an error code.
 */
int init_manager(void);

/// \brief Releases the resources of the manager and terminates all the sessions.
void release_manager(void);

/** \brief Create a new session for the specified file.
 * \param[in] pathname The pathname of the file in which the session will be created.
 * \param[in] flags The flags that specify the permissions on the file.
 * \param[in] ppid The pid of the process that wants to create the session.
 * \returns a pointer to an ::incarnation object, containing all the info on the current incarnation or an error code.
 */
struct incarnation* create_session(const char* pathname, int flags, pid_t pid);

/** \brief Closes a session.
 * \param[in] pathname The pathname to the original file.
 * \param[in] fdes The file descriptor of a session incarnation.
 * \param[in] pid The owner process pid.
 * \param[out] incarnation_pathname the pathname for the incarnation that is being closed.
 * \return 0 on success or an error code.
 * \todo handle tha absence of the `O_WRITE` flag when closing.
 */
int close_session(char* pathname, int fdes, pid_t pid,const char** incarnation_pathname);
#endif
