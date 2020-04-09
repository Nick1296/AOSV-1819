/** \file
 * \brief Implementation of the _Session Information_ submodule.
 */

#include "session_info.h"
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

 ///The device kobject provided during `init_info()`.
 struct kobject* dev_kobj;


/** \brief The function used to read the SysFS `active_sessions_num` attribute file.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
 * \returns The number of bytes read (in [0,PAGE_SIZE]).
 * The file content is the number of active sessions.
 */
 ssize_t active_sessions_num_show(struct kobject *obj, struct kobj_attribute *attr, char* buf){
	 return scnprintf(buf,PAGE_SIZE,"%d",sessions_num);
}

 ///The kernel attribute that will contain the number of open sessions.
 struct kobj_attribute kattr= __ATTR_RO(active_sessions_num);

/** \brief The function used to read the SysFS `active_incarnations_num` attribute file.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
 * \returns The number of bytes read (in [0,PAGE_SIZE]).
 * The file content returned is the number of active incarnations for the current original file.
 */
 ssize_t active_incarnations_num_show(struct kobject *obj, struct kobj_attribute *attr, char* buf){
	 struct sess_info* info=container_of(attr,struct sess_info,inc_num_attr);
	 return scnprintf(buf,PAGE_SIZE,"%d",info->inc_num);
}

/** \brief The function used to read the SysFS incarnations attribute files.
 * \param[in] obj The kobject that has the attribute being read.
 * \param[in] attr The aatribute of the kobject that is being read.
 * \param[out] buf The buffer (which is PAGE_SIZE bytes long) that contains the file contents.
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

/** We add an attribute called `active_sessions_num` to the SessionFS device kernel object, which is only readable and its content is the number of active sessions.
 */
 int init_info(struct kobject* device_kobj){
	int res;
	printk(KERN_DEBUG "SessionFS session info: Initializing the info on the active sessions");
	//we initialize the session_num
	sessions_num=0;
	//we create the session_num attribute
	//we add the attribute to the device
	res=sysfs_create_file(device_kobj,&(kattr.attr));
	if(res<0){
		return res;
	}
	printk(KERN_DEBUG "SessionFS session info: info added successfully");
	dev_kobj=device_kobj;
	return 0;
}

void release_info(void){
	printk(KERN_DEBUG "SessionFS session info: removing info on active sessions");
///we remove the 'active_sessions_num' attribute from the device
sysfs_remove_file(dev_kobj,&(kattr.attr));
}

/** We add a new folder in sysfs which is represented by the given kobject.
 * The `::session` kobject, represented by the `::sess_info` member the `::session` object will be created as a child of `::dev_kobj`, and the `::dev_kobj` reference counter will be incremented.
 */
int add_session_info(const char* name,struct sess_info* session){
	int res,i,namelen;
	char * f_name=NULL;
	printk(KERN_DEBUG "SessionFS session info: adding a info on a new original file: %s",name);
	f_name=kzalloc(sizeof(char*)*PATH_MAX, GFP_KERNEL);
	if(f_name==NULL){
		return -ENOMEM;
	}
	///We also format the filename substituting '/' with '-'.
	namelen=strlen(name);
	for(i=0;i<namelen;i++){
		if(name[i]!='/'){
			f_name[i]=name[i];
		} else {
			f_name[i]='-';
		}
	}
	session->f_name=f_name;
	printk(KERN_DEBUG "SessionFS session info: formatted filename: %s",f_name);
	kobject_get(dev_kobj);
	//we add the session kobject as a child of the root kobject
	session->kobj=kobject_create_and_add(f_name,dev_kobj);
	if(!session->kobj){
		kfree(f_name);
		session->f_name=NULL;
		kobject_put(dev_kobj);
		return -ENOMEM;
	}
	printk(KERN_DEBUG "SessionFS session info: folder created, adding info on the active incarnations number");
	///Finally, initialize the number of incarnations as a kobj_attribute.
	session->inc_num=0;
	session->inc_num_attr.attr.name="active_incarnations_num";
	session->inc_num_attr.attr.mode=VERIFY_OCTAL_PERMISSIONS(KERN_OBJ_PERM);
	session->inc_num_attr.show=active_incarnations_num_show;
	session->inc_num_attr.store=NULL;
	//we add the attribute to the device
	res=sysfs_create_file(session->kobj,&(session->inc_num_attr.attr));
	if(res<0){
		kfree(f_name);
		session->f_name=NULL;
		kobject_put(dev_kobj);
		kobject_del(session->kobj);
		return res;
	}
	printk(KERN_DEBUG "SessionFS session info: info added successfully");
	return 0;
}

/** Removes the entry corresponding to the given `::session`, represented by its `::sess_info` member, in the device SysFS folder.
 * To do so we also remove the `active_incarnations_num` file of the given `::session` and we decrement the reference counter of the device session kernel object.
 */
void remove_session_info(struct sess_info* session){
	printk(KERN_DEBUG "SessionFS session info: removing info on an original file");
	//we remove the number of incarnations attribute
	sysfs_remove_file(session->kobj,&(session->inc_num_attr.attr));
	//we put the root kobject
	kobject_put(dev_kobj);
	//we remove the entry from the parent folder
	kobject_del(session->kobj);
	kfree(session->f_name);
}

/** By adding a new incarnation we increment `active_sessions_num` and `active_incarnations_num` for the given `::session`, represented by its `::sess_info` member,
*  also, a kobject attribute is added to the given `::session` that has the process pid as filename and contains the process name.
* Finally the reference counter of the given `::session` is also incremented.
*/
int add_incarnation_info(struct sess_info* parent_session,struct kobj_attribute* incarnation,pid_t pid,int fdes){
	int res;
	//we allocate memory for the attribute name
	char* name=kzalloc(sizeof(char)*512, GFP_KERNEL);
	if(!name){
		return -ENOMEM;
	}
	printk(KERN_DEBUG "SessionFS session info: adding info on the incarnation created for process %d",pid);
//we initialize the attribute name
scnprintf(name,20,"%d_%d",pid,fdes);
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
		return res;
	}
	printk(KERN_DEBUG "SessionFS session info: info added successfully");
	return 0;
}

/** By adding a new incarnation we decrement `active_sessions_num` and `active_incarnations_num` for the given `::session`, represented by its `::sess_info` member.
 *  Also, the kobject attribute that has the process pid as filename and contains the process name is removed from the given `::session`.
 * Finally the reference counter of the given `::session` is also decremented.
 */
void remove_incarnation_info(struct sess_info* parent_session,struct kobj_attribute* incarnation){
	printk(KERN_DEBUG "SessionFS session info: removing info on an incarnation");
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
	printk(KERN_DEBUG "SessionFS session info: info removed");
}
