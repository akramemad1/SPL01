#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define INITIAL_CAP 10

static void trim_newline(char *line) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
}

static void print_prompt(void) {
    printf("PicoShell > ");
    fflush(stdout);
}

/* ---------- Argument Parsing ---------- */

static char **parse_args(char *line, int *count) {
    int cap = INITIAL_CAP;
    char **args = (char **)malloc(cap * sizeof(char *));
    if (!args) return NULL;

    *count = 0;
    char *token = strtok(line, " ");

    while (token) {
        if (*count >= cap - 1) {
            cap *= 2;
            char **tmp = (char **)realloc(args, cap * sizeof(char *));
            if (!tmp) {
                free(args);
                return NULL;
            }
            args = tmp;
        }
        args[(*count)++] = strdup(token);
        token = strtok(NULL, " ");
    }

    args[*count] = NULL;
    return args;
}

static void free_args(char **args, int count) {
    for (int i = 0; i < count; i++) {
        free(args[i]);
    }
    free(args);
}

/* ---------- Built-ins ---------- */

static int cmd_echo(char **args, int count) {
    for (int i = 1; i < count; i++) {
        printf("%s", args[i]);
        if (i < count - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

static int cmd_pwd(void) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s\n", cwd);
        return 0;
    } else {
        perror("pwd");
        return 1;
    }
}

static int cmd_cd(char **args, int count) {
    const char *path = (count > 1) ? args[1] : getenv("HOME");
    if (chdir(path) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", path);
        return 1;
    }
    return 0;
}

/* ---------- External ---------- */

static int run_external(char **args) {
    pid_t pid = fork();

    if (pid == 0) {
        execvp(args[0], args);
        fprintf(stderr, "%s: command not found\n", args[0]);
        exit(127);
    } 
    else if (pid > 0) {
        int wstatus;
        waitpid(pid, &wstatus, 0);
        return WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
    } 
    else {
        perror("fork");
        return 1;
    }
}

/* ---------- Dispatcher ---------- */

static int execute(char **args, int count) {
    if (count == 0) return 0;

    if (strcmp(args[0], "exit") == 0) {
        printf("Good Bye\n");
        exit(0);
    } 
    else if (strcmp(args[0], "echo") == 0) {
        return cmd_echo(args, count);
    } 
    else if (strcmp(args[0], "pwd") == 0) {
        return cmd_pwd();
    } 
    else if (strcmp(args[0], "cd") == 0) {
        return cmd_cd(args, count);
    } 
    else {
        return run_external(args);
    }
}

/* ---------- Main ---------- */

int picoshell_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char *line = NULL;
    size_t len = 0;
    int status = 0;

    while (1) {
        print_prompt();

        if (getline(&line, &len, stdin) == -1) {
            break;
        }

        trim_newline(line);
        if (line[0] == '\0') continue;

        int count = 0;
        char **args = parse_args(line, &count);
        if (!args) {
            fprintf(stderr, "Memory allocation error\n");
            continue;
        }

        status = execute(args, count);
        free_args(args, count);
    }

    free(line);
    return status;
}
