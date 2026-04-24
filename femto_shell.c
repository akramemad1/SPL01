#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIZE 30000

static void trim_newline(char *buf) {
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
}

static void print_prompt(void) {
    printf("welcome to my shell, enter your command!\n> ");
    fflush(stdout);
}

static int handle_echo(char *args) {
    if (args) {
        printf("%s\n", args);
    } else {
        printf("\n");
    }
    return 0;
}

static int execute_command(char *input) {
    char *cmd = strtok(input, " ");
    if (!cmd) return 0;

    if (strcmp(cmd, "exit") == 0) {
        printf("Good Bye\n");
        exit(0);
    } 
    else if (strcmp(cmd, "echo") == 0) {
        char *args = strtok(NULL, "");
        return handle_echo(args);
    } 
    else {
        printf("Invalid command\n");
        return 1;
    }
}

int femtoshell_main(int argc, char *argv[]) {
    char buf[SIZE];
    int status = 0;

    while (1) {
        print_prompt();

        if (!fgets(buf, sizeof(buf), stdin)) {
            return status;
        }

        if (buf[0] == '\n') continue;

        trim_newline(buf);
        status = execute_command(buf);
    }
}
