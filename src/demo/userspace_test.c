#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "../shared_lib/libsessionfs.h"

int main(int argc, char** argv){
	int ret,fd,dev,sess_num;
	char *buf=malloc(sizeof(char)*1024);
	printf("reading current session path\n");
	dev=open("/dev/SessionFS_dev", O_RDONLY);
	assert(ret>0);
	ret=read(dev, buf, 10);
	printf("session path %s\n", buf);
	printf("reading module information\n");
 	sess_num=open("/sys/devices/virtual/SessionFS_class/SessionFS_dev/active_sessions_num", O_RDONLY);
	perror("can't open active session file\n");
	assert(sess_num>0);
	memset(buf,0,sizeof(char)*1024);
	ret=read(sess_num,buf,10);
	printf("active sessions %s\n",buf);
	ret=open("test.txt", O_CREAT | O_SESS | O_RDWR);
	perror("error during opening the file with O_SESS\n");
	printf("open result: %d\n",ret);
	assert(ret>0);
	ret=read(sess_num,buf,10);
	sprintf("active sessions %s\n",buf);
	sprintf(buf,"test string");
	fd=ret;
	ret=write(fd,buf,strlen(buf));
	close(fd);
	close(dev);
	close(sess_num);
	free(buf);
	return 0;
}
