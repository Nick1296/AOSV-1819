#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "../shared_lib/libsessionfs.h"

int change_sess_path(char* path){
	int ret;
	char *buf=malloc(sizeof(char)*PATH_MAX);
	printf("reading current session path...\n");
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		perror("can't get session path");
		return ret;
	}
	printf("session path %s\n", buf);
	printf("changing session path...\n");
	ret=write_sess_path(path,strlen(path));
	if(ret<0){
		perror("can't change session path");
		return ret;
	}
	printf("re-reading to check if the change has been made successfully\n");
	memset(buf,0,sizeof(char)*PATH_MAX);
	ret=get_sess_path(buf,PATH_MAX);
	if(ret<0){
		perror("can't get session path");
		return ret;
	}
	printf("new session path: %s\n",buf);
	free(buf);
	return 0;
}


/** \brief A general functionality test.
 * We will test that all the features of the module are functional by simulating the common usage pattern that a single
 * process could have.
 * \todo exapling better as the function becomes more complicated.
 */
void func_test(void){
	int ret,fd,sess_num;
	char *buf=malloc(sizeof(char)*1024);
	ret=change_sess_path(".");
	assert(ret>=0);
	printf("reading module information\n");
	sess_num=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
	if(sess_num<0){
		perror("can't open active session file\n");
	}
	assert(sess_num>0);
	memset(buf,0,sizeof(char)*1024);
	ret=read(sess_num,buf,10);
	printf("active sessions %s\n",buf);
	ret=open("test.txt", O_CREAT | O_SESS | O_RDWR);
	if(ret<0){
		perror("error during opening the file with O_SESS\n");
	}
	assert(ret>0);
	fd=ret;
	printf("re-reading session number to see if it has changed...\n");
	memset(buf,0,sizeof(char)*1024);
	ret=read(sess_num,buf,10);

	printf("active sessions %s\n",buf);
	sprintf(buf,"test string");
	printf("writing a test string into the file\n");
	ret=write(fd,buf,strlen(buf));
	printf("closing the session\n");
	close(fd);
	close(sess_num);
	free(buf);
}

int main(int argc, char** argv){
	func_test();
}
