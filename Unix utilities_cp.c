#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#define COUNT 100
int cp_main(int argc, char* argv[])
{
        char buf[COUNT];
        int num_read;
        if(argc>=3) // if argc < 3 then throw error
        {
                int fd = open(argv[1],O_RDONLY);
                if(fd < 0 )
                {
                        printf("Could not open source file!\n");
                        exit(-1);
                }
                int fdDest = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC , 0644);
                if(fdDest < 0 )
                {
                        printf("Could not open Destination file!\n");
                        exit(-1);
                }

                while( ( num_read = read(fd,buf,COUNT) )  > 0)
                {
                        if(write(fdDest,buf,num_read) < 0)
                        {
                                printf("write failed!\n");
                                exit(-2);
                        }
                }
                close(fd);
                close(fdDest);
        }
        else {  exit(-3);}
        return 0 ;
}
