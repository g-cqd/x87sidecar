#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/sysctl.h>
int main(int argc,char**argv){
  int t=0; size_t s=sizeof t; sysctlbyname("sysctl.proc_translated",&t,&s,0,0);
  printf("hello pid=%d translated=%d argv0=%s\n",getpid(),t,argv[0]);
  if(argc>1) sleep(atoi(argv[1]));
  return 0; }
