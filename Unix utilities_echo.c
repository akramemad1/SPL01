#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int echo_main(int argc, char *argv[]) {
    // Write your code here
    // Do not write a main() function. Instead, deal with echo_main() as the main function of your program.
            if(argc>1)
        {
                for(int i = 1;i<argc;i++)
                {
                        printf("%s",argv[i]);
                        if(i != argc-1)
                        printf(" ");
                }
        }
        printf("\n");
        return 0;
}
