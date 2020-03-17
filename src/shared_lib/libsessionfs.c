
/// Enables RTLD_NEXT macro.
#define _GNU_SOURCE
// dlsym function
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>

///For the definition of the `O_SESS` flag.
#include "libsessionfs.h"

///For the ioctls methods exposed by our char device
#include "../kmodule/device_sessionfs.h"

///The path of our device file
#define DEV_PATH "/dev/SessionFS_dev"

///we use a typedef to alias the function pointer to the libc `open`.
typedef int (*orig_open_type)(const char* pathname, int flags);

/// we use a typedef to alis the function pointer to the libc `close`.
typedef int (*orig_close_type)(int filedes);

// we define the function pointers to the open and the close
orig_open_type orig_open;
orig_close_type orig_close;

/** \brief a program constructor which saves the original value for the `open` and `close` symbols.
* \return 0 on success nad -1 on error, setting errno.
*/
static __attribute__((constructor)) int init_method(void){
	orig_open = (orig_open_type) dlsym(RTLD_NEXT, "open");
	if(orig_open==NULL){
		printf("libsessionfs: can't load libc open: %s",dlerror());
		errno=ENODATA;
		return -1;
	}
	orig_close = (orig_close_type) dlsym(RTLD_NEXT,"close");
	if(orig_open==NULL){
		printf("libsessionfs: can't load libc close: %s",dlerror());
		errno=ENODATA;
		return -1;
	}
	return 0;
}

/**
 * \brief Wraps the close determining if it must call the libc `close` or the SessionFS module.
 * \param[in] filedes file descriptor to deallocate, same as libc `open`'s `fildes`.
 * \returns 0 on success, -1 on error, setting errno to indicate the error value.
 * \todo see if is better to locate the incarnation filename from the library (by reading from `/proc/self/fd/filedes`)
 */
int close(int __fd){
	int res;
	//we prepare a sess_params struct to remove the incarnation
	struct sess_params* params=malloc(sizeof(struct sess_params));
	memset(params,0,sizeof(struct sess_params));
	if(params==NULL){
		errno=ENOMEM;
		return -1;
	}
	char* inc_path=malloc(sizeof(char)*PATH_MAX);
	memset(inc_path,0,sizeof(char)*PATH_MAX);
	if(inc_path==NULL){
		free(params);
		errno = ENOMEM;
		return -1;
	}
	char *tmp_path=malloc(sizeof(char)*PATH_MAX);
	memset(tmp_path,0,sizeof(char)*PATH_MAX);
	if(tmp_path==NULL){
		free(params);
		free(inc_path);
		errno=ENOMEM;
		return -1;
	}
	//we read the incarnation path from the file table
	res=snprintf(tmp_path,sizeof(char)*PATH_MAX,"/proc/self/fd/%d",__fd);
	if(res < 0){
		free(params);
		free(inc_path);
		free(tmp_path);
		return res;
	}
	res=readlink(tmp_path,inc_path,sizeof(char)*PATH_MAX);
	if(res<0){
		free(tmp_path);
		free(params);
		free(inc_path);
		return res;
	}
	printf("libsessionfs: path to the file that must be closed: %s\n",inc_path);
	//we trasform the path to the incarnation to the path to the session
	char *inc_text=NULL,*sess_path=NULL;
	sess_path=malloc(sizeof(char)*PATH_MAX);
	if(sess_path==NULL){
		free(tmp_path);
		free(params);
		free(inc_path);
		errno=ENOMEM;
		return -1;
	}
	memcpy(sess_path,inc_path,sizeof(char)*PATH_MAX);
	res=snprintf(tmp_path,sizeof(char)*PATH_MAX,"_incarnation_%d_",getpid());
	if(res < 0){
		free(params);
		free(inc_path);
		free(tmp_path);
		free(sess_path);
		return res;
	}
	//we search for '_incarnation_[pid]_' in the file path, to understand if is an incarnation to be closed
	inc_text=strstr(sess_path,tmp_path);
	free(tmp_path);
	if(inc_text==NULL){
		//the file that we need to close is not an incarnation, so we call libc's close
		printf("libsessionfs: file descriptor is not a session incarnation, using libc close\n");
		free(params);
		free(inc_path);
		free(sess_path);
		return orig_close(__fd);
	} else {
		printf("libsessionfs: detected a session incarnation, adjusting path to match original file\n");
		//we remove the '_incarnation_...' to obtain the original file path
		memset(inc_text,0,strlen(inc_text));
		printf("libsessionfd: original file path: %s\n",sess_path);
	}

	params->orig_path=sess_path;
	params->filedes=__fd;
	params->pid=getpid();
	//we open the device
	int dev;
	printf("libsessionfs: opening char device\n");
	dev=orig_open(DEV_PATH,O_WRONLY);
	if(dev<0){
		free(sess_path);
		free(params);
		free(inc_path);
		return dev;
	}
	printf("libsessionfs: calling kernel module to remove the session\n");
	//we remove the incarnation
	//we retry if we receive ENODEV, since the module will notice that there is a valid session to be closed
	res=-ENODEV;
	res=ioctl(dev,IOCTL_SEQ_CLOSE,params);
	free(sess_path);
	if(res<0){
		if(res==-ENODEV){
			printf("libsessionfs: device disabled, retry closing");
			errno=ENODEV;
			return -1;
		}
		printf("libsessionfs: error during session close\n");
		orig_close(dev);
		free(inc_path);
		free(params);
		errno=-res;
		return -1;
	}
	printf("libsessionfs: calling libc close to remove the file descriptor\n");
	//we call libc close
	res=orig_close(__fd);
	if(res<0){
		orig_close(dev);
		free(inc_path);
		free(params);
		return res;
	}
	printf("libsessionfs: removing the incarnation file\n");
	//we delete the incarnation
	res=orig_close(dev);
	if(res<0){
		printf("libsesionfs: error using libc's close to close the incarnation\n");
		return res;
	}
	res=remove(inc_path);
	if(res<0){
		printf("libsessionfs: error durin the elimination of the incarnation file\n");
		return res;
	}
	free(inc_path);
	free(params);
	return res;
}

/**
 * \brief Wraps the open determining if it must call the libc `open` or the SessionFS module.
 * \param[in] pathname The pathname of the file to be opened, same usage an type of the libc `open`'s `pathname`.
 * \param[in] flags flags to determine the file status flag and the access modes, same as the libc `open`'s `oflag`, however a possible flag is the ::O_SESS flag which enables the session semantic.
 * \returns It will return a file descriptor if the operation is successful, both for the libc version and for the module return value.
 *
 * This function will check for the presence of the ::O_SESS flag, if this flag is present, the function will perform an ioctl call to the SessionFS kernel module, to open a new session for the given pathname.
 * If the ::O_SESS flag is not present, the function will call the libc implementation of the `open` systemcall.
 */
int open(const char* __file, int __oflag, ...){
	int flag=0, res=0,dev;
	char *slash="/";
	//we get the session path from the device
	char *sess_path=malloc(sizeof(char)*PATH_MAX), *file_path=malloc(sizeof(char)*PATH_MAX), *path=NULL;
	memset(file_path,0,sizeof(char)*PATH_MAX);
	//we convert (if necessary) the give pathname to an absolute pathname
	if(__file[0]!='/'){
		printf("libsessionfs: converting pathname to absolute...\n");
		path=realpath(__file,file_path);
		if(path==NULL){
			//the user might want to create a file
			if(errno==ENOENT && (__oflag & O_CREAT)){
				path=realpath(".",file_path);
				printf("libsessionfs: absolute path for current directory: %s\n",file_path);
				if(path==NULL){
					return -1;
				}
				strncat(file_path,slash,strlen(slash));
				strncat(file_path,__file,sizeof(char)*(PATH_MAX-strlen(file_path)+1));
			}
		}
	} else {
		strncpy(file_path,__file,sizeof(char)*PATH_MAX);
	}
	printf("libsessionfs: pathname: %s, absolute pathname: %s\n",__file,file_path);
	memset(sess_path,0,sizeof(char)*PATH_MAX);
	printf("libsessionfs: reading the current session path\n");
	res=get_sess_path(sess_path,PATH_MAX);
	if(res<0){
		free(file_path);
		free(sess_path);
		return res;
	}
	printf("libsessionfs: current session path: %s \t given pathname: %s\n", sess_path,file_path);
	//we check if the file is in the right path
	path=strstr(file_path,sess_path);
	// check for the presence of the O_SESS flag
	flag=__oflag & O_SESS;
	free(sess_path);
	if(flag==O_SESS && path!=NULL){
		printf("libsessionfs: detected O_SESS flag and correct path\n");
		//we open the device
		dev=orig_open(DEV_PATH,O_WRONLY);
		if(dev<0){
			free(file_path);
			return dev;
		}
		printf("libsessionfs: allocating and filling a sess_params struct\n");
		//we prepare an instance of the sess_params struct
		struct sess_params* params=malloc(sizeof(struct sess_params));
		memset(params,0,sizeof(struct sess_params));
		if(params==NULL){
			errno=ENOMEM;
			orig_close(dev);
			free(file_path);
			return -1;
		}
		params->orig_path=file_path;
		params->flags=__oflag;
		params->pid=getpid();
		printf("libsessionfs: calling kernel module to create a new incarnation\n");
		res=ioctl(dev,IOCTL_SEQ_OPEN,params);
		if(res<0){
			perror("libsessionfs: error creating the session, trying to close the invalid session");
			close(params->filedes);
			orig_close(dev);
			free(file_path);
			free(params);
			errno=-res;
			return -1;
		}
		//we check if the created session is valid
		if(params->valid != VALID_SESS){
			printf("libsessionfs: session invalid: closing\n");
			//if is invalid we need to call our close
			close(params->filedes);
			errno=-EAGAIN;
			free(file_path);
			free(params);
			return -1;
		}
		free(file_path);
		res=params->filedes;
		free(params);
		printf("libsessionfs: session opened successfully\n");
		return res;
	} else {
		printf("libsessionfs: calling libc open\n");
		//we flip the O_SESS flag just to be sure we aren't giving an unexpected flag to libc open.
		return orig_open(__file, __oflag & ~O_SESS);
	}
}

/**
 * Reads from the device located at ::DEV_PATH.
 */
int get_sess_path(char* buf,int bufsize){
	int dev=0,res=0;
	dev=orig_open(DEV_PATH, O_RDONLY);
	if(dev<0){
		return dev;
	}
	res=read(dev,buf,bufsize);
	if(res<0){
		errno=-res;
		return -1;
	}
	return res;
}

/** Writes on the device at ::DEV_PATH.
 */
int write_sess_path(char* path){
	int dev=-1, res=0;
	char* abs_path=NULL;
	printf("libsessionfs: converting %s path to absolute\n",path);
	abs_path=realpath(path,abs_path);
	printf("libsessionfs: absolute path: %s\n",abs_path);
	if(abs_path==NULL){
		return -1;
	}
	dev=orig_open(DEV_PATH,O_WRONLY);
	if(dev<0){
		return dev;
	}
	res=write(dev,abs_path,strlen(abs_path));
	if(res<0){
		errno=-res;
		return -1;
	}
	return res;
}
