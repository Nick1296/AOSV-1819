/** \file
 * \brief Implementation for the session_info module
 */

///The module that handles the session information
#include "session_info.h"
#include "session_types.h"
//for spinlocks APIs
#include <linux/spinlock.h>
//for container_of
#include <linux/kernel.h>
//for get_pid_task
#include <linux/pid.h>
//for struct task_struct
#include <linux/sched.h>
//for memory APIs
#include <linux/slab.h>

///Kernel objects attributes are read only, since we only get information on session
#define KERN_OBJ_PERM 0444

///The number of opened sessions
 int sessions_num;

///We set a global lock to make sure that kobjects and attributes are manipulated one at time
spinlock_t kobj_lock; /// \todo verify if its really necessary

/** \brief The function used to read the SysFS `active_sessions_num` attribute file.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[in,out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
 * \returns The number of bytes read (in [0,PAGE_SIZE]).
 * The file content is the number of active sessions
 */
 ssize_t active_sessions_num_show(struct kobject *obj, struct kobj_attribute *attr, char* buf){
	 return scnprintf(buf,PAGE_SIZE,"%d",sessions_num);
}

/** \brief The function used to read the SysFS `active_incarnations_num` attribute file.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[in,out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
 * \returns The number of bytes read (in [0,PAGE_SIZE]).
 * The file content returned is the number of active incarnations for the current original file.
 */
 ssize_t active_incarnations_num_show(struct kobject *obj, struct kobj_attribute *attr, char* buf){
	 struct sess_info* info=container_of(&obj,struct sess_info,kobj);
	 return scnprintf(buf,PAGE_SIZE,"%d",info->inc_num);
}

/** \brief The function used to read the SysFS incarnations attribute files.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[in,out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
 * \returns The number of bytes read (in [0,PAGE_SIZE]).
 * The file content is the process name that corresponds to the pid used as filename.
 */
 ssize_t proc_name_show(struct kobject *obj, struct kobj_attribute *attr, char* buf){
	 struct incarnation* inc=container_of(attr,struct incarnation,inc_attr);
	 //we get the task struct containing the process name
	 struct task_struct* task;
	 struct pid* pid;
	 char* name="ERROR: process not found";
	 pid=find_get_pid(inc->owner_pid);
	 if(!IS_ERR(pid) && pid){
		task=get_pid_task(pid,PIDTYPE_PID);
		if(!IS_ERR(task) && task){
			name=task->comm;
		}
	 }
	 return scnprintf(buf,PAGE_SIZE,"%s",name);
}

/// \todo TODO check if a kset can be useful to group the session original files

///The device kobject provided during ::init_info.
struct kobject* dev_kobj;

///The kernel attribute that will contain the number of open sessions.
struct kobj_attribute kattr= __ATTR_RO(active_sessions_num);

/** We add an attribute called `active_sessions_num` to the SessionFS device, which is only readable and its content is the number of active sessions.
 */
 int init_info(struct kobject* device_kobj){
	int res;
	//we initialize the spinlock
	spin_lock_init(&kobj_lock);
	//we initialize the session_num
	sessions_num=0;
	//we create the session_num attribute
	//we add the attribute to the device
	res=sysfs_create_file(device_kobj,&(kattr.attr));
	if(res<0){
		return res;
	}
	dev_kobj=device_kobj;
	return 0;
}

void release_info(void){
//we remove the file from the device
sysfs_remove_file(dev_kobj,&(kattr.attr));
}

/** We add a new folder in sysfs which is represented by the given kobject.
 * The ::session kobject will be created as a child of ::dev_kobj.
 */
int add_session_info(const char* name,struct sess_info* session){
	int res;
	//we get the lock
	spin_lock(&kobj_lock);
	//we get the root kobject
	kobject_get(dev_kobj);
	//we add the session kobject as a child of the root kobject
	session->kobj=kobject_create_and_add(name,dev_kobj);
	if(!session->kobj){
		kobject_put(dev_kobj);
		spin_unlock(&kobj_lock);
		return -ENOMEM;
	}
	//we initialize the number of incarnations as a kobj_attribute
	session->inc_num=0;
	session->inc_num_attr.attr.name="active_incarnations_num";
	session->inc_num_attr.attr.mode=VERIFY_OCTAL_PERMISSIONS(KERN_OBJ_PERM);
	session->inc_num_attr.show=active_incarnations_num_show;
	session->inc_num_attr.store=NULL;
	//we add the attribute to the device
	res=sysfs_create_file(session->kobj,&(session->inc_num_attr.attr));
	spin_unlock(&kobj_lock);
	if(res<0){
		kobject_put(dev_kobj);
		kobject_del(session->kobj);
		return res;
	}
	return 0;
}

/** Removes the corresponding entry in the parent SysFS folder
 */
void remove_session_info(struct sess_info* session){
	spin_lock(&kobj_lock);
	//we remove the number of incarnations attribute
	sysfs_remove_file(session->kobj,&(session->inc_num_attr.attr));
	//we put the root kobject
	kobject_put(dev_kobj);
	//we remove the entry from the parent folder
	kobject_del(session->kobj);
	spin_unlock(&kobj_lock);
}

/** The kobject attribute has the process pid as filename and contains the process name.
 * By adding a new incarnation we increment the global number of sessions and the number of incarnation for the original file;
 */
int add_incarnation_info(struct sess_info* parent_session,struct kobj_attribute* incarnation,pid_t pid){
	int res;
	//we allocate memory for the attribute name
	char* name=kzalloc(sizeof(char)*512, GFP_KERNEL);
	if(!name){
		return -ENOMEM;
	}
//we initialize the attribute name
scnprintf(name,512,"%d",pid);
//we get the lock
	spin_lock(&kobj_lock);
	//we increment the global number of sessions
	sessions_num++;
	//we increment the number of incarnations for the original file
	parent_session->inc_num++;
	//we get the parent kobject
	kobject_get(parent_session->kobj);
	//we create the attribute
	incarnation->attr.name=name;
	incarnation->attr.mode=VERIFY_OCTAL_PERMISSIONS(KERN_OBJ_PERM);
	incarnation->show=proc_name_show;
	incarnation->store=NULL;
	//we add the attribute to the device
	res=sysfs_create_file(parent_session->kobj,&(incarnation->attr));
	if(res<0){
		kobject_put(parent_session->kobj);
		sessions_num--;
		parent_session->inc_num--;
		spin_unlock(&kobj_lock);
		return res;
	}
	spin_unlock(&kobj_lock);
	return 0;
}

void remove_incarnation_info(struct sess_info* parent_session,struct kobj_attribute* incarnation){
	spin_lock(&kobj_lock);
	//we decrement the global number of sessions
	sessions_num--;
	//we decrement the number of incarnations for the original file
	parent_session->inc_num--;
	//we free the incarnation name string
	kfree(incarnation->attr.name);
	//we remove the number of incarnations attribute
	sysfs_remove_file(parent_session->kobj,&(incarnation->attr));
	//we put the parent kobject
	kobject_put(parent_session->kobj);
	spin_unlock(&kobj_lock);
}

int get_sessions_num(void){
	return sessions_num;
}