#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../shared_lib/libsessionfs.h"

/** \breif Use libsessionfs APIs to change the session path to ::path.
 * \param[in] path The path in which the session path should be changed.
 * \returns 0 or -1 in case of error. (errno is set by libsessionfs functions).
 */
int change_sess_path(char* path){
	int ret;
	char *buf=malloc(sizeof(char)*PATH_MAX);
	if(buf==NULL){
		perror("Can't allocate buffer to store session parh");
		return -1;
	}
	memset(buf,0,PATH_MAX);
	printf("reading current session path...\n");
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		perror("can't get session path");
		free(buf);
		return ret;
	}
	printf("session path %s\n", buf);
	printf("changing session path...\n");
	ret=write_sess_path(path);
	if(ret<0){
		free(buf);
		perror("can't change session path");
		return ret;
	}
	printf("re-reading to check if the change has been made successfully\n");
	memset(buf,0,sizeof(char)*PATH_MAX);
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		free(buf);
		perror("can't get session path");
		return ret;
	}
	printf("new session path: %s\n",buf);
	free(buf);
	return 0;
}


/** \brief A general functionality test.
 * \param[in] files_max The number of files to be used during the test.
 * We will test that all the features of the module are functional by simulating the common usage pattern that a single
 * process could have, for a random number of files that goest from 0 to ::files_max.
 * In detail we execute the folliwing operations for each file:
 *  * read the active_sessions_num pseudofile;
 *  * open the file with the ::O_SESS flag;
 *  * we check active_sessions_num;
 *  * we read active_incarnations_num for the current file and the file with our pid for each session we have created to ensure that they return meaningful values;
 *  * we test write, read and lseek writing a random number of bytes from 0 to 1MB, by writing the pid serveral times:
 *    - we write the process pid;
 *    - we seek back to where we hae written the last pid;
 *    - we read what we have written;
 *    - we check that there aren't mismatches;
 *  * we seek to the begining, middle and end of the file;
 *  * we sleep for 60 seconds at random, to have some files born from the same incarnation in the concurrent test;
 *  * we close the opened  file;
 *  * we check active_sessions_num;
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
			snprintf(err_buf,1024,"%d: can't open active_sessions_num file",pid);
			perror(err_buf);
		}
		assert(sess_num_fd>0);

		memset(buf,0,sizeof(char)*PATH_MAX);
		ret=read(sess_num_fd,buf,PATH_MAX);
		printf("%d: active sessions: %s\n",pid,buf);
		close(sess_num_fd);

		// we open a file with the O_SESS flag
		if(rand()%2){
			printf("sleeping for 10 seconds before opening the file");
			sleep(10);
		}
		ret=open(fnames[file_i], O_CREAT | O_SESS | O_RDWR);
		fd[file_i]=ret;
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error during opening the file with O_SESS",pid);
			perror(err_buf);
			break;
		}

		// we check that active_sessions_num has incremented
		printf("%d: re-reading session number to see if it has changed...\n",pid);
		sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
		if(sess_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: can't open active_sessions_num file",pid);
			perror(err_buf);
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
			snprintf(err_buf,1024,"%d: can't open active active_incarnations_num file for file %s",pid,fnames[file_i]);
			perror(err_buf);
		}
		assert(inc_num_fd>0);

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
			snprintf(err_buf,1024,"%d: can't open pid file, for file %s",pid,fnames[file_i]);
			perror(err_buf);
		}
		assert(proc_name_fd>0);

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
				snprintf(err_buf,1024,"%d: can't seeek at the and of the file %s",pid,fnames[file_i]);
				perror(err_buf);
			}
		}else {
			printf("%d: owerwriting file %s\n",pid, fnames[file_i]);
		}
		assert(ret>=0);
		//we write many times the pid of the current process to match the written file size
		while(written <= content_size){
			ret=write(fd[file_i],dummy_content,dummy_content_len);
			if(ret<dummy_content_len){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: can't write the pid on file %s",pid,fnames[file_i]);
				perror(err_buf);
			}
			assert(ret>=dummy_content_len);
			written+=ret;
			//we move back to check what we have wrote
			ret=lseek(fd[file_i],-ret,SEEK_CUR);
			if(ret<0){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: error while seeking backwards in the file",pid);
				perror(err_buf);
			}
			assert(ret>=0);
			// we check that we have wrote the correct information
			memset(buf,0,sizeof(char)*PATH_MAX);
			ret=read(fd[file_i],buf,dummy_content_len);
			ret=strncmp(dummy_content,buf,dummy_content_len);
			if(ret!=0){
				printf("%d error during write on file %s: file contents mismatch\n",pid ,fnames[file_i]);
			}
			assert(ret==0);
		}

		//llseek test
		printf("%d: seeking at the beginning of file %s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],0,SEEK_SET);
		assert(ret>=0);

		printf("%d: seeking in the middle of file:%s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],content_size/2,SEEK_SET);
		assert(ret>=0);

		printf("%d: seeking at the end of the file: %s\n",pid,fnames[file_i]);
		ret=lseek(fd[file_i],0,SEEK_END);
		assert(ret>=0);
	}

	printf("%d: closing opened files\n",pid);
	for(file_i=0;file_i<file_num;file_i++){
		//we leave purposefully some sessions opened, to see if the module can find and close correctly these sessions when unmounted
		if(rand()%2){
			//we determine if we need to close the file now randomically (so when we are in concurency some files are bound to be born from the same incarnation)
			if(rand()%2){
				printf("%d: sleeping for 10 seconds before closing file: %s\n",pid,fnames[file_i]);
				sleep(10);
			}
			// we try to close the opened session
			printf("%d: closing the file: %s\n",pid,fnames[file_i]);
			ret=close(fd[file_i]);
			if(ret<0){
				memset(err_buf,0,sizeof(char)*1024);
				snprintf(err_buf,1024,"%d: can't close incarnation of %s\n",pid,fnames[file_i]);
				perror(err_buf);
			}
			assert(ret>=0);
		} else {
			printf("%d: leaving file %s open\n",pid,fnames[file_i]);
		}

		// we check active_sessions_num
		printf("%d: re-reading session number to see if it has changed...\n",pid);
		sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
		if(sess_num_fd<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d can't open active_sessions_num file",pid);
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
 * We change the session path to '/home', then we open a file with ::O_SESS, in the current folder.
 * Afterwards we change the session path to '/mnt' and we open a file with ::O_SESS again in the current folder.
 * Then we close both files.
 */
void sess_change_test(void){
	int f1,f2,ret,pid;
	pid=getpid();
	//we change the session path to the current directory
	ret=change_sess_path(".");
	assert(ret>=0);
	printf("%d: opening a file with O_SESS in the current directory\n",pid);
	ret=open("sess_change_test1.txt", O_CREAT | O_SESS | O_RDWR);
	if(ret<0){
		perror("error during opening the file with O_SESS");
	}
	f1=ret;
	printf("%d:changing session path to '/mnt' and trying to open another file with O_SESS in the same position as before\n",pid);
	ret=change_sess_path("/mnt");
	ret=open("sess_change_test2.txt", O_CREAT | O_SESS | O_RDWR);
	if(ret<0){
		perror("error during opening the file with O_SESS not in the current session path");
	}
	f2=ret;
	printf("%d:we close both files\n",pid);
	ret=close(f1);
	ret=close(f2);
}

/** \brief Test file semantic with a session opened when forking
 * We open a file with session semantic then we fork and read and write on this file with both child and father processes.
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
		snprintf(err_buf,1024,"%d: can't open active_sessions_num file",pid);
		perror(err_buf);
	}
	assert(sess_num_fd>0);

	memset(buf,0,sizeof(char)*PATH_MAX);
	ret=read(sess_num_fd,buf,PATH_MAX);
	printf("%d: active sessions: %s\n",pid,buf);
	close(sess_num_fd);

	// we open a file with the O_SESS flag
	ret=open("fork_test.txt", O_CREAT | O_SESS | O_RDWR);
	fd=ret;
	if(ret<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: error during opening the file with O_SESS",pid);
		perror(err_buf);
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
	printf("%d: dummy_content: %s lenght: %d\n",pid,dummy_content,dummy_content_len);
	// we check that active_sessions_num has incremented
	printf("%d: re-reading session number to see if it has changed...\n",pid_o);
	sess_num_fd=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
	if(sess_num_fd<0){
		memset(err_buf,0,sizeof(char)*1024);
		snprintf(err_buf,1024,"%d: can't open active_sessions_num file",pid_o);
		perror(err_buf);
	}
	memset(buf,0,sizeof(char)*PATH_MAX);
	read(sess_num_fd,buf,PATH_MAX);
	printf("%d: active sessions: %s\n",pid_o,buf);

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
		snprintf(err_buf,1024,"%d: can't open active active_incarnations_num file for file %s",pid_o,"fork_test.txt");
		perror(err_buf);
	}

	memset(buf,0,sizeof(char)*PATH_MAX);
	read(inc_num_fd,buf,PATH_MAX);
	printf("%d: active_incarnations_num: %s for file %s\n",pid_o,buf,"fork_test.txt");
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
		snprintf(err_buf,1024,"%d: can't open pid file, for file %s",pid_o,"fork_test.txt");
		perror(err_buf);
	}

	memset(buf,0,sizeof(char)*PATH_MAX);
	read(proc_name_fd,buf,PATH_MAX);
	printf("%d: process name: %s\n",pid_o,buf);
	close(proc_name_fd);

	// write and read test
	printf("%d: writing a test string into file %s\n",pid_o,"fork_test.txt");
	//we determine the (approximate) size of the our write operations from 0 to ~ 1MB
	content_size=rand()% (1 << 5);
	content_size=(content_size/dummy_content_len+1)*dummy_content_len;
	printf("%d: write size on %s: %d bytes\n",pid,"fork_test.txt",content_size);
	written=0;
	//we write many times the pid of the current process to match the written file size
	while(written <= content_size){
		ret=write(fd,dummy_content,dummy_content_len);
		if(ret<dummy_content_len){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: can't write the pid on file %s",pid_o,"fork_test.txt");
			perror(err_buf);
		}
		written+=ret;
		//we move back to check what we have wrote
		ret=lseek(fd,-ret,SEEK_CUR);
		if(ret<0){
			memset(err_buf,0,sizeof(char)*1024);
			snprintf(err_buf,1024,"%d: error while seeking backwards in the file",pid_o);
			perror(err_buf);
		}
		// we check that we have wrote the correct information
		memset(buf,0,sizeof(char)*PATH_MAX);
		ret=read(fd,buf,dummy_content_len);
		ret=strncmp(dummy_content,buf,dummy_content_len);
		if(ret!=0){
			printf("%d error during write on file %s: file contents mismatch\n",pid_o ,"fork_test.txt");
		}
	}
	//we pause one of the two processes to gice the change to the other to write on the file, but only in some cases.
	if((pid_o+pid)%2){
		printf("%d: sleeping for 10 seconds\n",pid_o);
		sleep(10);
	}
	printf("%d: closing the file\n",pid_o);
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
 * We change the session path to '/home' then we execute a single-thread test of most of the functionalities, using ::func_test.
 * Then we check that everything works as specified when changing the session path with an opened session.
 * \todo Finally we execute a multi-thread test on most of the functionalities.
 */
int main(int argc, char** argv){
	int ret,file_max=0,process_max=0, process_num,i,pid;
	char* base_fname=NULL;
	if(argc<3){
		printf("Usage: LD_PRELOAD=[ path to libsessionfs.so] LD_LIBRARY_PATH=[path to libsessionfs folder] demo [max processes number] [max files number]");
		return -1;
	}
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
			srand(i);
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
}
