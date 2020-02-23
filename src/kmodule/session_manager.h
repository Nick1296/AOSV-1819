/** \file session_manager.h
 * \brief APIs used to interact with the session manger.
 * This file contains the APIs of the session manager, which are used to initialize and release the manager and to add
 * and remove sessions.
 */
#ifndef SESS_MAN_H
#define SESS_MAN_H

#include <linux/types.h>

///The structs that represent sessions, incarnation and their informations.
#include "session_types.h"

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
 * \param[in] fdes The file descriptor of a session incarnation.
 * \param[in] pid The owner process pid.
 * \param[out] incarnation_pathname the pathname for the incarnation that is being closed.
 * \return 0 on success or an error code.
 * \todo handle tha absence of the `O_WRITE` flag when closing.
 */
int close_session(int fdes, pid_t pid,const char** incarnation_pathname);
#endif
