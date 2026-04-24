#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int pwd_main()
{
        char* name;

        if( (name = getcwd(NULL,0)) == NULL)
        {
                exit(-1);
        }
        else
        {
                printf("%s\n",name);
        }
        return 0;
}
