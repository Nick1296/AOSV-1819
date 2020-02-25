/** \file
 * Struct definition common to all the module that need to manage session informations
 */

#ifndef SESS_TYPES_H
#define SESS_TYPES_H


#include <linux/kobject.h>

/** \struct sess_info
 * \param kobj The session kernel object.
 * \param inc_num_attr The kernel object attribute that represents the number of incanration for the original file.
 * \param inc_num The actual number of open incarnation for the original file.
 */
struct sess_info{
	struct kobject* kobj;
	struct kobj_attribute inc_num_attr;
	int inc_num;
};

/** \struct incarnation
 * \brief Informations on an incarnation of a file.
 * \param next Next incarnation on the list.
 * \param file The struct file that represents the incarantion file.
 * \param inc_attr \todo document!
 * \param pathname The pathanme of the incarnation file.
 * \param filedes File descriptor of the incarnation.
 * \param owner_pid Pid of the process that has requested the incarnation.
 * \param status Contains the error code that could have invalidated the incarnation. If its value is less than 0 then the incarnation is invalid and must be closed as soon as possible.
 */
struct incarnation{
	struct llist_node next;
	struct file* file;
	struct kobj_attribute inc_attr;
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
 * \param info Informations on the current original file, represented by ::sess_info struct.
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
	struct sess_info info;
	struct file* file;
	const char* pathname;
	rwlock_t sess_lock;
	int valid;
};

#endif