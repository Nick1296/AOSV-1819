/** \file
 * \brief Handles the kobjects for the session manager.
 * This file is used to create and manage kobjects for the session manager.
 */
#ifndef SESSION_INFO_H
#define SESSION_INFO_H

#include <linux/kobject.h>
#include <linux/types.h>

///The structs that represent sessions, incarnation and their informations.
#include "session_types.h"

///The name of the Kobject representing the session manager in SysFS
#define SESS_KOBJ_NAME "SessionFS_info"

///Each attribute group has the same name, but different attributes according to the parent kobject.
#define ATTR_GROUP_NAME "info"

/** \brief initialiezes the SessionFS kobject with general informations about the running sessions.
 * \param[in] device_kobj The SessionFS char device kernel object, in which contains the info on all sessions.
 * \returns 0 on success, an error code on failure.
 */
int init_info(struct kobject* device_kobj);


/** \brief Removes the SessionFS information from the device provided in ::init_info.
 */
void release_info(void);

/** \brief Adds a new kobject representing an original file, under the SessionFS kobject.
 * \param[in] name The name of the created kobject.
 * \param[in,out] session The information on the session, represente by a struct ::sess_info.
 * \returns a struct kobject* on success, null if the kobject hasn't been created or an error code.
 */
int add_session_info(const char* name,struct sess_info* session);

/** \brief Removes a session kobject
 * \param[in] session the informations on the session to be removed, represented by a struct ::sess_info.
 */
void remove_session_info(struct sess_info* session);

/** \brief Adds a new kobject attribute representing an incarnation.
 * \param[in] parent_session The session which has generated the incarnation, represented by a struct ::sess_info.
 * \param[in] incarnation The incarnation to be added.
 * \param[in] pid The pid of the process that owns the incarnation.
 * \param[in] fdes The file descriptor that identifies the incarnation in the process.
 * \returns 0 on success, or an error code.
 */
int add_incarnation_info(struct sess_info* parent_session,struct kobj_attribute* incarnation,pid_t pid, int fdes);

/** \brief Removes the kobject attribute incarnation from a kobject
 * \param[in] parent_session The session which has generated the incarnation, represented by a struct ::sess_info.
 * \param[in] incarnation The incarnation to be added.
 */
void remove_incarnation_info(struct sess_info* parent_session, struct kobj_attribute* incarnation);

/** \brief Get the active session number.
 * \returns The number of the current active sessions.
 */
int get_sessions_num(void);
#endif
