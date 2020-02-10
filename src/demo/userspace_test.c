///we define the O_SESS flag which will enable sessions
#define O_SESS 00000004
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv){
	int ret;
	ret=open("test.txt",O_SESS | O_CREAT);
	printf("open result: %d\n",ret);
	close(ret);
	return 0;
}
