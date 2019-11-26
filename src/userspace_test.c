#define O_SESS 00000004
#include <stdio.h>
#include<fcntl.h>
// simple test that tries to open a file in a directory enabling session semtics and checks the open result.
int main(char* argv, int argc){
	int ret;
	ret=open("/home/nick1296/Workspace/test.txt",O_SESS | O_CREAT);
	printf("open result: %d\n",ret);
	return 0;
}
