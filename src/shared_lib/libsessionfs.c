/** \file libsessionfs.c
 * \brief Implementation of the userspace shared library.
 *
 * Used to provide a trasparent interface to the userspace application, that can use the libc open and close functions, along with the ::O_SESS flag to work with sessions. To change session path instead, it can use the get_sess_path() and write_sess_path() utility functions, to avoid the need to comminucate directly with the `SessionFS_dev` device.
*/

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
#include <stdarg.h>

///For the definition of the `::O_SESS` flagm ioctl numbers and sess_params struct.
#include "libsessionfs.h"

///The path of our device file
#define DEV_PATH "/dev/SessionFS_dev"

///A typedef that aliases the function pointer to the libc `open`.
typedef int (*orig_open_type)(const char* pathname, int flags);

///A typedef to aliases the function pointer to the libc `close`.
typedef int (*orig_close_type)(int filedes);

/// Global variable that holds the function pointer for libc `open`
orig_open_type orig_open;
/// Global variable that holds the function pointer for libc `close`
orig_close_type orig_close;

/** \brief A program constructor which saves the original value for the `open` and `close` symbols.
* \return 0 on success or -1 on error, setting errno.
* We save the original open and close since the library needs to understand when it's necessary to use the char device
* and when the libc implementation must be used.
* To do so we save them we use the `dlsym` function and we define two new types to avoid using a function pointer directly: `::orig_open_type` and `::orig_close_type`, these are simple typedefs that wrap the function poitner for libc `open` and `close`.
*
*/
static __attribute__((constructor)) int init_method(void){
	orig_open = (orig_open_type) dlsym(RTLD_NEXT, "open");
	if(orig_open==NULL){
		printf("libsessionfs: error: can't load libc open: %s",dlerror());
		errno=ENODATA;
		return -1;
	}
	orig_close = (orig_close_type) dlsym(RTLD_NEXT,"close");
	if(orig_open==NULL){
		printf("libsessionfs: error: can't load libc close: %s",dlerror());
		errno=ENODATA;
		return -1;
	}
	return 0;
}

/**
 * \brief Wraps the close determining if it must call the libc `close` or the SessionFS module.
 * \param[in] fd file descriptor to deallocate, same as libc `open`'s `fildes`.
 * \returns 0 on success, -1 on error, setting errno to indicate the error value.
 *
 * To determine if libc close must be used, the file pathname is read from `/proc/self/fd`, then `readlink` is used to resolve
 * the pathname and make it absolute, finally, if this pathname contains the `_incarnation_[pid]_` substring then it must be closed
 * by issuing an ioctl with number `::IOCTL_SEQ_CLOSE` to the `SessionFS_dev` device.
 * Otherwise the libc `close` is called.
 * A ::sess_params struct is used to pass parameters to the char device when necessary and after the device completes it operations
 * libc `close` is called to remove the file descriptor and `remove` is called to delete the incarnation file from the disk.
 * If the return value from the ioctl is `ENODEV` the the device was temporarly disabled and the operation must be retried.
 *
 */
int close(int fd){
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
		errno = ENOMEM;
		return -1;
	}
	//we read the incarnation path from the file table
	res=snprintf(tmp_path,sizeof(char)*PATH_MAX,"/proc/self/fd/%d",fd);
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
		return orig_close(fd);
	} else {
		printf("libsessionfs: detected a session incarnation, adjusting path to match original file\n");
		//we remove the '_incarnation_...' to obtain the original file path
		memset(inc_text,0,strlen(inc_text));
		printf("libsessionfs: original file path: %s\n",sess_path);
	}
	params->orig_path=sess_path;
	params->filedes=fd;
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
		orig_close(dev);
		if(res==-ENODEV){
			printf("libsessionfs: error: device disabled, retry closing");
			errno=ENODEV;
			return -1;
		}
		printf("libsessionfs: error during session close\n");
		free(inc_path);
		free(params);
		errno=-res;
		return -1;
	}
	res=orig_close(dev);
	if(res<0){
		printf("libsessionfs: error using libc's close to close the device\n");
		return res;
	}
	printf("libsessionfs: calling libc close to remove the file descriptor\n");
	//we call libc close
	res=orig_close(fd);
	if(res<0){
		free(inc_path);
		free(params);
		return res;
	}
	printf("libsessionfs: removing the incarnation file\n");
	res=remove(inc_path);
	if(res<0){
		printf("libsessionfs: error during the elimination of the incarnation file\n");
		return res;
	}
	free(inc_path);
	free(params);
	return res;
}

/**
 * \brief Wraps the open determining if it must call the libc `open` or the SessionFS module.
 * \param[in] pathname The pathname of the file to be opened, same usage an type of the libc `open`'s `pathname`.
 * \param[in]  flags Flags to determine the file status flag and the access modes, same as the libc `open`'s `oflag`, however a possible flag is the ::O_SESS flag which enables the session semantic.
 * \param[in] mode The permission to set if a file must be created, (when `O_CREAT` is specified).
 * \returns It will return a file descriptor if the operation is successful, both for the libc version and for the module return value.
 *
 * This function will check for the presence of the ::O_SESS flag and if the path of the file to be opened is has as a substring the session path.
 *
 * If the checks are successful, the function will perform an ioctls call to the SessionFS kernel module, via the `SessionFS_dev` device, to open a new session for the given pathname.
 * Otherwise, the function will call the libc implementation of the `open` systemcall.
 *
 * To check that the path has a substring `realpath` is used, to convert the pathname to absolute.
 * If `realpath` fails with `ENOENT` the path provided might be the relative path to a file that must be created, so the path of the current diretory is used as the file path.
 *
 * To perform the ioctl the ::IOCTL_SEQ_OPEN number is used and struct ::sess_params is filled and passed as an argument, to provide all the necessary informations to the device.
 *
 * If the opened session is not valid, the function will call `close()` to remove the invalid session in a clean way and the function will fail with `EAGAIN`.
 *
 */
int open(const char* pathname, int flags, ...){
	int flag=0, res=0,dev,mode=-1;
	char *slash="/";
	//we check if mode and if it was we get it as an optional parameter
	if(flags & O_CREAT){
		va_list arg;
		va_start (arg, flags);
		mode = va_arg (arg, int);
		va_end (arg);
	}
	//we get the session path from the device
	char *sess_path=malloc(sizeof(char)*PATH_MAX), *file_path=malloc(sizeof(char)*PATH_MAX), *path=NULL;
	if(sess_path==NULL || file_path==NULL){
		printf("libsessionfs: error: not enough memory\n");
		errno=-ENOMEM;
		return -1;
	}
	memset(file_path,0,sizeof(char)*PATH_MAX);
	//we convert (if necessary) the give pathname to an absolute pathname
	if(pathname[0]!='/'){
		printf("libsessionfs: converting pathname to absolute...\n");
		path=realpath(pathname,file_path);
		if(path==NULL){
			//the user might want to create a file
			if(errno==ENOENT && (flags & O_CREAT)){
				path=realpath(".",file_path);
				printf("libsessionfs: absolute path for current directory: %s\n",file_path);
				if(path==NULL){
					return -1;
				}
				strncat(file_path,slash,strlen(slash));
				strncat(file_path,pathname,sizeof(char)*(PATH_MAX-strlen(file_path)+1));
			}else{
				printf("libsessionfs: error: path conversion failed\n");
				free(sess_path);
				free(file_path);
				return -1;
			}
		}
	} else {
		strncpy(file_path,pathname,sizeof(char)*PATH_MAX);
	}
	printf("libsessionfs: pathname: %s, absolute pathname: %s\n",pathname,file_path);
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
	flag=flags & O_SESS;
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
			orig_close(dev);
			errno=ENOMEM;
			free(file_path);
			return -1;
		}
		params->orig_path=file_path;
		params->flags=flags;
		params->mode=mode;
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
		res=orig_close(dev);
		if(res<0){
			printf("libsessionfs: error using libc's close to close the device\n");
			return res;
		}
		//we check if the created session is valid
		if(params->valid != VALID_SESS){
			printf("libsessionfs: error: session invalid: closing\n");
			//if is invalid we need to call our close
			close(params->filedes);
			errno=-EAGAIN;
			free(file_path);
			free(params);
			return -1;
		}
		free(file_path);
		printf("libsessionfs: session opened successfully, fd:%d\n",params->filedes);
		res=params->filedes;
		free(params);
		return res;
	} else {
		free(file_path);
		printf("libsessionfs: calling libc open\n");
		/// When creating a new file we need to call `creat` since we have the symbol only for the open with two parametrs.
		if(flags & O_CREAT){
			res=creat(pathname,mode);
			if(res<0){
				return res;
			}
		}
		//we flip the O_SESS flag just to be sure we aren't giving an unexpected flag to libc open.
		return orig_open(pathname, flags & ~O_SESS & ~O_CREAT);
	}
}

/**
 * This function is a simple utility function that reads from the `SessionFS_dev` device, located at ::DEV_PATH, the current session path and places it in the buffer provided by the caller.
*/
int get_sess_path(char* buf,int bufsize){
	int dev=0,res=0;
	dev=orig_open(DEV_PATH, O_RDONLY);
	if(dev<0){
		perror("libsessionfs: can't open SessionFS_dev");
		return dev;
	}
	res=read(dev,buf,bufsize);
	if(res<0){
		orig_close(dev);
		errno=-res;
		return -1;
	}
	res=orig_close(dev);
	if(res<0){
		printf("libsessionfs: error using libc's close to close the device\n");
		return res;
	}
	return res;
}

/**
 * This function is a simple utility function that writes on the `SessionFS_dev` device, located at ::DEV_PATH, the content of the buffer provided by the user; before doing so however, it uses the `realpath` function to make sure that the path provided to char device is an absolute path.
 */
int write_sess_path(char* path){
	int dev=-1, res=0;
	char* abs_path=NULL;

	printf("libsessionfs: converting %s path to absolute\n",path);
	abs_path=realpath(path,abs_path);
	if(abs_path==NULL){
		return -1;
	}

	dev=orig_open(DEV_PATH,O_WRONLY);
	if(dev<0){
		perror("libsessionfs: can't open SessionFS_dev");
		return dev;
	}

	//adding string terminator, may overwrite the lasta caracter bus is needed.
	abs_path[PATH_MAX]='\0';
	printf("libsessionfs: absolute path: %s\n",abs_path);
	res=write(dev,abs_path,strlen(abs_path));
	free(abs_path);
	if(res<0){
		orig_close(dev);
		errno=-res;
		return -1;
	}
	res=orig_close(dev);
	if(res<0){
		printf("libsessionfs: error using libc's close to close the device\n");
		return res;
	}
	return res;
}

/**
 * To power down the device we only need to execute an ioctl with number `::IOCTL_SEQ_SHUTDOWN` and the devce will proceed accordingly.
 */
int device_shutdown(void){
	int dev,res,active_sessions;
	//we open the device
	dev=orig_open(DEV_PATH,O_RDONLY);
	if(dev<0){
		return dev;
	}
	//we request the device shutdown
	res=ioctl(dev,IOCTL_SEQ_SHUTDOWN,&active_sessions);
	if(res<0){
		printf("libsessionfs: error: device shutdown failed,%d session active, try again later\n",active_sessions);
		orig_close(dev);
		errno=-res;
		res=-1;
		return res;
	}else{
		printf("libsessionfs: device shutdown successful\n");
	}
	res=orig_close(dev);
	if(res<0){
		printf("libsessionfs: error using libc's close to close the device\n");
		return res;
	}
	return res;
}
