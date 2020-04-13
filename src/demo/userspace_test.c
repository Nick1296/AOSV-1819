/** \file userspace_test.c
 * \brief Userpsace program that will test the kernel module using the libsessionfs.c shared library.
 *
 * It will "simulate" a general use-case in which the module can be used, by operating on a pseudorandom number of files
 * with a pseudorandom number of processes.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../shared_lib/libsessionfs.h"

///Permissions to be used when calling `open()`
#define DEFAULT_PERM 0644

/** \brief Use libsessionfs APIs to change the session path to `path`.
 * \param[in] path The path in which the session path should be changed.
 * \returns 0 or -1 in case of error. (`errno` is set by libsessionfs functions).
 *
 * This utility function uses `get_sess_path()` and `write_sess_path()` to read the current session path, change it and
 * display the results to the user, testing the session path change feature.
 */
int change_sess_path(char* path){
	int ret;
	char *buf, *err_buf;
	err_buf=malloc(sizeof(char)*1024);
	if(err_buf==NULL){
		perror("can't allocate error buffer");
		return -1;
	}

	buf=malloc(sizeof(char)*PATH_MAX);
	if(buf==NULL){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: Can't allocate buffer to store session path",getpid());
		perror(err_buf);
		free(err_buf);
		return -1;
	}
	memset(buf,0,PATH_MAX);
	printf("%d reading current session path...\n",getpid());
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: can't get session path",getpid());
		perror(err_buf);
		free(err_buf);
		free(buf);
		return ret;
	}
	printf("%d session path %s\n",getpid(), buf);
	printf("%d changing session path...\n",getpid());
	ret=write_sess_path(path);
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: can't change session path",getpid());
		perror(err_buf);
		free(err_buf);
		free(buf);
		return ret;
	}
	printf("%d re-reading session path\n",getpid());
	memset(buf,0,sizeof(char)*PATH_MAX);
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: can't get session path",getpid());
		perror(err_buf);
		free(err_buf);
		free(buf);
		return ret;
	}
	printf("%d new session path: %s\n",getpid(),buf);
	free(buf);
	free(err_buf);
	return 0;
}


/** \brief A general functionality test.
 * \param[in] files_max The number of files to be used during the test.
 * \param[in] base_fname The string used to begin the filename of all the used files.
 *
 * This function will test that all the features of the module are functional by simulating the common usage pattern that a single
 * process could have, for a random number of files that goes from 0 to `files_max`.
 * In detail we execute the following operations for each file:
 *  * read the `active_sessions_num` pseudofile;
 *  * we can sleep at random for 1 second, to give the other process, if any, a chance to override the incarnation;
 *  * open the file with the `::O_SESS` flag;
 *  * we check `active_sessions_num` pseudofile;
 *  * we read `active_incarnations_num` and the file with our pid for each session we have created to ensure that they return meaningful values;
 *  * we test `write`, `read` and `lseek` writing a random number of bytes from 0 to 1MB, by writing the pid serveral times:
 *    - before writing we decide at random to overwrite the file or to append our contents, to have a chance to see different pids in the original file;
 *    - we write the process pid;
 *    - we seek back to where we have written the last pid;
 *    - we read what we have written;
 *    - we check that there aren't mismatches;
 *  * we seek to the beginning, middle and end of the file;
 *  * we sleep for 1 second at random, to have some files born from the same incarnation in the concurrent test;
 *  * we can close the opened file or leave it open at random to test how the module handles session with a dead owner;
 *  * we check `active_sessions_num` pseudofile;
 */
void func_test(int files_max,char* base_fname){
	int ret,*fd=NULL,sess_num_fd,inc_num_fd,proc_name_fd,i,file_i,file_num=0,content_size,written,dummy_content_len,pid;
	char *buf=NULL, *buf2=NULL,**fnames=NULL,*dummy_content=NULL,*err_buf=NULL;
	pid=getpid();
	//we generate the number of files that will be used, from 0 to files_max
	if(files_max==1){
		file_num=files_max;
	}else{
		while(file_num==0){
			file_num=rand()%files_max;
		}
	}
	printf("%d: \t using %d files\n",pid,file_num);

	fd=malloc(sizeof(int)*file_num);
	dummy_content=malloc(sizeof(char)*20);
	err_buf=malloc(sizeof(char)*1024);
	fnames=malloc(sizeof(char*)*file_num);
	buf=malloc(sizeof(char)*PATH_MAX);
	buf2=malloc(sizeof(char)*PATH_MAX);
	assert(buf!=NULL && buf2!=NULL && fnames!=NULL && dummy_content!=NULL && err_buf!=NULL && fd!=NULL);

	memset(fd,-1,sizeof(int)*file_num);
	//we initialize the dummy content
	memset(dummy_content,0,sizeof(char)*20);
	snprintf(dummy_content,sizeof(char)*20,"\t %d \t",pid);
	dummy_content_len=strlen(dummy_content);
	printf("%d: dummy_content: %s lenght: %d\n",pid,dummy_content,dummy_content_len);

	for(file_i=0;file_i< file_num;file_i++){
		//we determine the filename
		fnames[file_i]=malloc(sizeof(char)*PATH_MAX);
		memset(fnames[file_i],0,sizeof(char)*PATH_MAX);
		snprintf(fnames[file_i],PATH_MAX,"%s_%d.txt",base_fname,rand()%files_max);
		printf("%d: working on %s\n",pid,fnames[file_i]);

		//we read active_incarnations_num for the first time
		sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
		if(sess_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error: can't open active_sessions_num file",pid);
			file_num=file_i+1;
			break;
		}

		memset(buf,0,sizeof(char)*PATH_MAX);
		ret=read(sess_num_fd,buf,PATH_MAX);
		printf("%d: active sessions: %s\n",pid,buf);
		close(sess_num_fd);

		// we open a file with the O_SESS flag
		if(rand()%2){
			printf("sleeping for 1 second before opening the file");
			sleep(1);
		}
		ret=open(fnames[file_i], O_CREAT | O_SESS | O_RDWR,DEFAULT_PERM);
		fd[file_i]=ret;
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error during opening the file with O_SESS",pid);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}

		// we check that active_sessions_num has incremented
		printf("%d: re-reading session number to see if it has changed...\n",pid);
		sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
		if(sess_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error: can't open active_sessions_num file",pid);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}
		memset(buf,0,sizeof(char)*PATH_MAX);
		read(sess_num_fd,buf,PATH_MAX);
		printf("%d: active sessions: %s\n",pid,buf);
		close(sess_num_fd);

		realpath(fnames[file_i],buf2);
		for(i=0;i<strlen(buf2);i++){
			if(buf2[i]=='/'){
				buf2[i]='-';
			}
		}

		//we read active_incarnations_num for the opened session
		memset(buf,0,sizeof(char)*PATH_MAX);
		snprintf(buf,PATH_MAX,"/sys/devices/virtual/SessionFS_class/SessionFS_dev/%s/%s",buf2,"active_incarnations_num");
		inc_num_fd=open(buf,O_RDONLY);
		if(inc_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error: can't open active active_incarnations_num file for file %s",pid,fnames[file_i]);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}

		memset(buf,0,sizeof(char)*PATH_MAX);
		read(inc_num_fd,buf,PATH_MAX);
		printf("%d: active_incarnations_num: %s for file %s\n",pid,buf,fnames[file_i]);
		close(inc_num_fd);

		memset(buf,0,sizeof(char)*PATH_MAX);
		memset(buf2,0,sizeof(char)*PATH_MAX);

		//we read the pid file for the opened session
		realpath(fnames[file_i],buf2);
		for(i=0;i<strlen(buf2);i++){
			if(buf2[i]=='/'){
				buf2[i]='-';
			}
		}

		snprintf(buf,PATH_MAX,"/sys/devices/virtual/SessionFS_class/SessionFS_dev/%s/%d_%d",buf2,pid,fd[file_i]);
		proc_name_fd=open(buf,O_RDONLY);
		if(proc_name_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error: can't open pid file, for file %s",pid,fnames[file_i]);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}

		memset(buf,0,sizeof(char)*PATH_MAX);
		read(proc_name_fd,buf,PATH_MAX);
		printf("%d: process name: %s\n",pid,buf);
		close(proc_name_fd);


		// write and read test
		printf("%d: writing a test string into file %s\n",pid,fnames[file_i]);
		//we determine the (approximate) size of the our write operations from 0 to ~ 1MB
		content_size=rand()% (1 << 20);
		content_size=(content_size/dummy_content_len+1)*dummy_content_len;
		printf("%d: write size on %s: %d bytes\n",pid,fnames[file_i],content_size);
		written=0;
		//we seek to pid*dummy_content_len in the file so we can have a chance to see the processes that have used this file
		if(rand()%2){
			printf("%d: appending content to %s\n",pid,fnames[file_i]);
			ret=lseek(fd[file_i],0,SEEK_END);
			if(ret<0){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: error: can't seeek at the and of the file %s",pid,fnames[file_i]);
				perror(err_buf);
				file_num=file_i+1;
				break;
			}
		}else {
			printf("%d: owerwriting file %s\n",pid, fnames[file_i]);
		}
		//we write many times the pid of the current process to match the written file size
		while(written <= content_size){
			ret=write(fd[file_i],dummy_content,dummy_content_len);
			if(ret<dummy_content_len){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: error: can't write the pid on file %s",pid,fnames[file_i]);
				perror(err_buf);
				break;
				ret=-1;
			}
			written+=ret;
			//we move back to check what we have wrote
			ret=lseek(fd[file_i],-ret,SEEK_CUR);
			if(ret<0){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: error while seeking backwards in the file",pid);
				perror(err_buf);
				break;
			}
			// we check that we have wrote the correct information
			memset(buf,0,sizeof(char)*PATH_MAX);
			ret=read(fd[file_i],buf,dummy_content_len);
			ret=strncmp(dummy_content,buf,dummy_content_len);
			if(ret!=0){
				printf("%d error during write on file %s: file contents mismatch\n",pid ,fnames[file_i]);
				ret=-1;
				break;
			}
		}
		if(ret<0){
			file_num=file_i+1;
			break;
		}

		//llseek test
		printf("%d: seeking at the beginning of file %s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],0,SEEK_SET);
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error while seeking at the begin of the file",pid);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}

		printf("%d: seeking in the middle of file:%s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],content_size/2,SEEK_SET);
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error while seeking in the middle of the file",pid);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}

		printf("%d: seeking at the end of the file: %s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],0,SEEK_END);
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error while seeking at the end of the file",pid);
			perror(err_buf);
			file_num=file_i+1;
			break;
		}
	}

	printf("%d: closing opened files\n",pid);
	for(file_i=0;file_i<file_num;file_i++){
		//we leave purposefully some sessions opened, to see if the module can find and close correctly these sessions when unmounted
		if(rand()%2){
			//we determine if we need to close the file now randomically (so when we are in concurency some files are bound to be born from the same incarnation)
			if(rand()%2){
				printf("%d: sleeping for 1 second before closing file: %s\n",pid,fnames[file_i]);
				sleep(1);
			}
			// we try to close the opened session
			printf("%d: closing the file: %s\n",pid,fnames[file_i]);
			ret=close(fd[file_i]);
			if(ret<0){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: error: can't close incarnation of %s\n",pid,fnames[file_i]);
				perror(err_buf);
			}
		} else {
			printf("%d: leaving file %s open\n",pid,fnames[file_i]);
		}

		// we check active_sessions_num
		printf("%d: re-reading session number to see if it has changed...\n",pid);
		sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
		if(sess_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d error: can't open active_sessions_num file",pid);
			perror(err_buf);
		}
		memset(buf,0,sizeof(char)*PATH_MAX);
		read(sess_num_fd,buf,PATH_MAX);
		printf("%d: active sessions: %s\n",pid,buf);
		close(sess_num_fd);
		free(fnames[file_i]);
	}

	free(buf);
	free(err_buf);
	free(buf2);
	free(fnames);
	free(dummy_content);
	free(fd);
}

/** \brief Test the semantic of sessions when the session path has changed.
 *
 * We change the session path to the current directory, then we open a file with `::O_SESS`, in the current directory.
 * Afterwards we change the session path to `/mnt` and we open a file with `::O_SESS` again in the current directory.
 * Then we close both files.
 * The filename of the files created starts with the `sess_change_test` string.
 */
void sess_change_test(void){
	int f1,f2,ret,pid;
	pid=getpid();
	char * err_buf=malloc(sizeof(char)*1024);
	if(err_buf==NULL){
		perror("can't allocate error buffer");
		return;
	}
	//we change the session path to the current directory
	change_sess_path(".");
	printf("%d: opening a file with O_SESS in the current directory\n",pid);
	ret=open("sess_change_test1.txt", O_CREAT | O_SESS | O_RDWR,DEFAULT_PERM);
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error during opening the file with O_SESS in the current session path",pid);
		perror(err_buf);
	}
	f1=ret;
	printf("%d:changing session path to '/mnt' and trying to open another file with O_SESS in the same position as before\n",pid);
	ret=change_sess_path("/mnt");
	ret=open("sess_change_test2.txt", O_CREAT | O_SESS | O_RDWR, DEFAULT_PERM);
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error during opening the file with O_SESS not in the current session path",pid);
		perror(err_buf);
		close(f1);
	}
	f2=ret;
	printf("%d:we close both files\n",pid);
	close(f1);
	close(f2);
	free(err_buf);
}

/** \brief Test file semantic with a session opened when forking.
 *
 * We open a file called `fork_test.txt` with session semantic then we execute the write test without appending done in `func_test()` on this file with both child and father processes.
 * This is done to see if using session preserves the original semantic of the `read`, `write` and `llseek` functions.
 * We can expect that one of the two processes will fail some operations on the file, since they have an intentional race condition on closing.
 */
void fork_test(void){
	int fd,sess_num_fd,inc_num_fd,pid,ret,proc_name_fd,content_size,dummy_content_len,written,i,pid_o;
	char *err_buf,*buf,*buf2,*dummy_content;

	err_buf=malloc(sizeof(char)*1024);
	dummy_content=malloc(sizeof(char)*20);
	buf=malloc(sizeof(char)*PATH_MAX);
	buf2=malloc(sizeof(char)*PATH_MAX);
	assert(buf!=NULL && buf2!=NULL && dummy_content!=NULL && err_buf!=NULL);

	pid=getpid();
	//we read active_incarnations_num for the first time
	sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
	if(sess_num_fd<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: can't open active_sessions_num file",pid);
		perror(err_buf);
		free(err_buf);
		free(buf);
		free(buf2);
		free(dummy_content);
	}
	assert(sess_num_fd>0);

	memset(buf,0,sizeof(char)*PATH_MAX);
	ret=read(sess_num_fd,buf,PATH_MAX);
	printf("%d: active sessions: %s\n",pid,buf);
	close(sess_num_fd);

	// we open a file with the O_SESS flag
	ret=open("fork_test.txt", O_CREAT | O_SESS | O_RDWR, DEFAULT_PERM);
	fd=ret;
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error during opening the file with O_SESS",pid);
		perror(err_buf);
		free(err_buf);
		free(buf);
		free(buf2);
		free(dummy_content);
	}
	assert(ret>0);

	pid_o=getpid();
	pid=fork();
	if(pid!=0){
		printf("%d: child pid:%d\n",pid_o,pid);
	}
	//we initialize the dummy content
	memset(dummy_content,0,20);
	snprintf(dummy_content,20,"\t %d \t",pid);
	dummy_content_len=strlen(dummy_content);
	printf("%d fork test %d: dummy_content: %s lenght: %d\n",pid_o,pid,dummy_content,dummy_content_len);
	// we check that active_sessions_num has incremented
	printf("%d fork test %d: re-reading session number to see if it has changed...\n",pid_o,pid);
	sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
	if(sess_num_fd<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error: can't open active_sessions_num file",pid_o);
		perror(err_buf);
	}
	memset(buf,0,sizeof(char)*PATH_MAX);
	read(sess_num_fd,buf,PATH_MAX);
	printf("%d fork test %d: active sessions: %s\n",pid_o,pid,buf);

	realpath("fork_test.txt",buf2);
	for(i=0;i<strlen(buf2);i++){
		if(buf2[i]=='/'){
			buf2[i]='-';
		}
	}

	//we read active_incarnations_num for the opened session
	memset(buf,0,sizeof(char)*PATH_MAX);
	snprintf(buf,PATH_MAX,"/sys/devices/virtual/SessionFS_class/SessionFS_dev/%s/%s",buf2,"active_incarnations_num");
	inc_num_fd=open(buf,O_RDONLY);
	if(inc_num_fd<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d fork test %d: error: can't open active active_incarnations_num file for file %s",pid_o,pid,"fork_test.txt");
		perror(err_buf);
	}

	memset(buf,0,sizeof(char)*PATH_MAX);
	read(inc_num_fd,buf,PATH_MAX);
	printf("%d fork test %d: active_incarnations_num: %s for file %s\n",pid_o,pid,buf,"fork_test.txt");
	close(inc_num_fd);

	memset(buf,0,sizeof(char)*PATH_MAX);
	memset(buf2,0,sizeof(char)*PATH_MAX);

	//we read the pid file for the opened session
	realpath("fork_test.txt",buf2);
	for(i=0;i<strlen(buf2);i++){
		if(buf2[i]=='/'){
			buf2[i]='-';
		}
	}

	snprintf(buf,PATH_MAX,"/sys/devices/virtual/SessionFS_class/SessionFS_dev/%s/%d_%d",buf2,pid_o,fd);
	proc_name_fd=open(buf,O_RDONLY);
	if(proc_name_fd<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d fork test %d: error: can't open pid file, for file %s",pid_o,pid,"fork_test.txt");
		perror(err_buf);
	}

	memset(buf,0,sizeof(char)*PATH_MAX);
	read(proc_name_fd,buf,PATH_MAX);
	printf("%d fork test %d: process name: %s\n",pid_o,pid,buf);
	close(proc_name_fd);

	// write and read test
	printf("%d fork test %d: writing a test string into file %s\n",pid_o,pid,"fork_test.txt");
	//we determine the (approximate) size of the our write operations from 0 to ~ 1MB
	content_size=rand()% (1 << 5);
	content_size=(content_size/dummy_content_len+1)*dummy_content_len;
	printf("%d fork test %d: write size on %s: %d bytes\n",pid_o,pid,"fork_test.txt",content_size);
	written=0;
	//we write many times the pid of the current process to match the written file size
	while(written <= content_size){
		ret=write(fd,dummy_content,dummy_content_len);
		if(ret<dummy_content_len){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d fork test %d: error, can't write the pid on file %s",pid_o,pid,"fork_test.txt");
			perror(err_buf);
		}
		written+=ret;
		//we move back to check what we have wrote
		ret=lseek(fd,-ret,SEEK_CUR);
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d fork test %d: error while seeking backwards in the file",pid_o,pid);
			perror(err_buf);
		}
		// we check that we have wrote the correct information
		memset(buf,0,sizeof(char)*PATH_MAX);
		ret=read(fd,buf,dummy_content_len);
		ret=strncmp(dummy_content,buf,dummy_content_len);
		if(ret!=0){
			printf("%d fork test %d error during write on file %s: file contents mismatch\n",pid_o,pid,"fork_test.txt");
		}
	}
	//we pause one of the two processes to gice the change to the other to write on the file, but only in some cases.
	if((pid_o+pid)%2){
		printf("%d fork test %d: sleeping for 1 second\n",pid_o,pid);
		sleep(1);
	}
	printf("%d fork test %d: closing the file\n",pid_o,pid);
	close(fd);
	if(pid==0){
		exit(0);
	} else {
		wait(NULL);
	}
	free(err_buf);
	free(buf);
	free(buf2);
	free(dummy_content);
}

/** \brief Testing of the kernel module
 * \param[in] argc Number of the given arguments, 3 is expected.
 * \param[in] argv The arguments given to the file; we expect two arguments, the maximum number of processes to be used in the test followed by the maximum number of files to be used by each process.
 *
 * We spawn a number of processes from 1 to the number given as first parameter, then in each child process we change the session path to the current directory using `change_sess_path()`, we execute `func_test()`, `sess_change_test()` and `fork_test()`.
 * If the maximum number of processes used is 1 then all the files created by `func_test()` have a filename that starts with the `single_process` string, otherwise with the `multi_process` string.
 */
int main(int argc, char** argv){
	int ret,file_max=0,process_max=0, process_num,i,pid;
	char* base_fname=NULL;
	if(argc<3){
		printf("Usage: LD_PRELOAD=[ path to libsessionfs.so] LD_LIBRARY_PATH=[path to libsessionfs folder] demo [max processes number] [max files number]");
		return -1;
	}
	srand(getpid());
	process_max=atoi(argv[1]);
	file_max=atoi(argv[2]);
	printf("Maximum number of files that can be used:%d\n",file_max);
	printf("Maximum number of processes used in the test:%d\n",process_max);
	if(process_max==1){
		printf("\n\n\n\t\t\t single process test \n");
		process_num=1;
		base_fname="single_process";
	} else {
		process_num=rand()%process_max;
		base_fname="multi_process";
		printf("\n\n\n\t\t\t multi process test with %d processes\n",process_num);
	}
	for(i=0;i<process_num;i++){
		pid=fork();
		assert(pid>=0);
		if(pid==0){
			srand(getpid());
			printf("\t\t\t%d -- changing session path to the current directory:\n",getpid());
			ret=change_sess_path(".");
			assert(ret>=0);
			printf("\t\t\t%d -- functionality test:\n",getpid());
			func_test(file_max,base_fname);
			//we test the behaviour of sessions when we change the session path
			printf("\n\n\n\t\t\t%d -- session change test\n",getpid());
			sess_change_test();
			printf("\n\n\n\t\t\t%d -- fork with opened session test\n",getpid());
			//we change the session path to the current directory
			ret=change_sess_path(".");
			assert(ret>=0);
			fork_test();
			exit(0);
		}
	}
	//we wait for all the child processes
	for(i=0;i<process_num;i++){
		wait(NULL);
	}
	///To be able remove the kernel module we need to power down the `SessionFS_dev` device, using the dedicated ioctl as the last operation on the device.
	printf("requesting device shutdown\n");
	ret= device_shutdown();
	if(ret<0){
		perror("error during device shutdown");
	}
	return ret;
}
