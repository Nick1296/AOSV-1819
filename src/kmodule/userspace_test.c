#define O_SESS 00000004
#include <stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include <string.h>
// simple test that tries to open a file in a directory enabling session semtics and checks the open result.
int main(char* argv, int argc){
	int ret,dev=-1;
	dev=open("/dev/SessionFS_dev", O_RDWR);
	printf("open result: %d\n",dev);
	char buffer[2048];
	ret=read(dev,buffer,1024);
	printf("%s\n",buffer);
	ret=write(dev,"/tmp",strlen("/tmp"));
	perror("write result");
	ret=read(dev,buffer,1024);
	printf("%s\n",buffer);
	close(dev);
	return 0;
}
