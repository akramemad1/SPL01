#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#define COUNT 100

int mv_main (int argc, char* argv[])
{
        // Fix: Changed to '==' since a standard mv requires exactly 3 arguments
        if(argc == 3)
        {
                // Fix: Use char instead of int for a byte buffer
                char buf[COUNT]; 
                
                int fdSource = open(argv[1], O_RDONLY);
                if(fdSource < 0)
                {
                        printf("Couldn't open source file!\n");
                        exit(1);
                }
                
                int fdDest = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC , 0644);
                if(fdDest < 0)
                {
                        printf("Couldn't open Destination file!\n");
                        close(fdSource);
                        exit(1);
                }
                
                int numRead;
                while((numRead = read(fdSource, buf, COUNT)) > 0)
                {
                        if(write(fdDest, buf, numRead) < 0)
                        {
                                printf("Could not write to destination!\n");
                                close(fdSource); // Good practice to close before exiting
                                close(fdDest);
                                exit(1);
                        }
                }
                
                close(fdSource);
                close(fdDest);
                unlink(argv[1]);
        }
        else 
        {
                printf("Usage: %s <source> <destination>\n", argv[0]);
                exit(1); // Exits cleanly with standard failure code
        }
        
        return 0; // Returns 0 on success as the test expects
}
