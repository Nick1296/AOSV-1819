/** \file session_manager.h
 * \brief APIs used to interact with the session manger, component of the _Session Manager_ submodule.
 *
 * This file contains the APIs of the session manager, which are used to initialize and release the manager and to add
 * and remove sessions.
 */
#ifndef SESS_MAN_H
#define SESS_MAN_H

#include <linux/types.h>

///The structs that represent sessions, incarnation and their informations.
#include "session_types.h"

/** \brief Initialization of the session manager data structures.
 * \returns 0 on success or an error code.
 */
int init_manager(void);

/** \brief Releases all the incarnations that are associated with a dead/zombie pid.
 * \returns the number of sessions associated with an active pid.
*/
int clean_manager(void);

/** \brief Create a new session for the specified file.
 * \param[in] pathname The pathname of the file in which the session will be created.
 * \param[in] flags The flags that specify the permissions on the file.
 * \param[in] pid The pid of the process that wants to create the session.
 * \param[in] mode The permissions to apply to newly created files.
 * \returns a pointer to an ::incarnation object, containing all the info on the current incarnation or an error code.
 */
struct incarnation* create_session(const char* pathname, int flags, pid_t pid, mode_t mode);

/** \brief Closes a session.
 * \param[in] pathname the pathname for the session containing the incarnation that is being closed.
 * \param[in] fdes The file descriptor of a session incarnation.
 * \param[in] pid The owner process pid.
 * \return 0 on success or an error code.
 */
int close_session(const char* pathname, int fdes, pid_t pid);
#endif
