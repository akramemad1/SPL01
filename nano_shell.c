#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>

#define MAX_VARS 100
#define MAX_VAR_LEN 256
#define INITIAL_CAP 10

typedef struct {
    char name[MAX_VAR_LEN];
    char value[MAX_VAR_LEN];
} Variable;

static Variable local_vars[MAX_VARS];
static int var_count = 0;

/* ---------- Variable Handling ---------- */

static Variable* find_variable(const char* name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(local_vars[i].name, name) == 0) {
            return &local_vars[i];
        }
    }
    return NULL;
}

static int set_variable(const char* name, const char* value) {
    Variable* var = find_variable(name);

    if (var) {
        strncpy(var->value, value, MAX_VAR_LEN - 1);
        var->value[MAX_VAR_LEN - 1] = '\0';
        return 1;
    }

    if (var_count >= MAX_VARS) {
        fprintf(stderr, "Error: Maximum number of variables reached\n");
        return 0;
    }

    strncpy(local_vars[var_count].name, name, MAX_VAR_LEN - 1);
    local_vars[var_count].name[MAX_VAR_LEN - 1] = '\0';

    strncpy(local_vars[var_count].value, value, MAX_VAR_LEN - 1);
    local_vars[var_count].value[MAX_VAR_LEN - 1] = '\0';

    var_count++;
    return 1;
}

/* ---------- Variable Substitution ---------- */

static char* substitute_variables(const char* input) {
    if (!input) return NULL;

    char* result = strdup(input);
    if (!result) return NULL;

    char* pos = result;

    while ((pos = strchr(pos, '$'))) {
        char* end = pos + 1;
        while (*end && (isalnum(*end) || *end == '_')) end++;

        int len = end - (pos + 1);
        if (len <= 0) {
            pos++;
            continue;
        }

        char name[MAX_VAR_LEN];
        strncpy(name, pos + 1, len);
        name[len] = '\0';

        Variable* var = find_variable(name);

        const char* replacement = var ? var->value : "";
        size_t new_len = strlen(result) - (len + 1) + strlen(replacement);

        char* new_str = (char*)malloc(new_len + 1);
        if (!new_str) {
            free(result);
            return NULL;
        }

        int prefix_len = pos - result;
        strncpy(new_str, result, prefix_len);
        new_str[prefix_len] = '\0';

        strcat(new_str, replacement);
        strcat(new_str, end);

        free(result);
        result = new_str;
        pos = result + prefix_len + strlen(replacement);
    }

    return result;
}

/* ---------- Parsing ---------- */

static char** parse_args(char* line, int* count) {
    int cap = INITIAL_CAP;
    char** args = (char**)malloc(cap * sizeof(char*));
    if (!args) return NULL;

    *count = 0;
    char* token = strtok(line, " ");

    while (token) {
        if (*count >= cap - 1) {
            cap *= 2;
            char** tmp = (char**)realloc(args, cap * sizeof(char*));
            if (!tmp) {
                free(args);
                return NULL;
            }
            args = tmp;
        }

        char* sub = substitute_variables(token);
        args[(*count)++] = sub ? sub : strdup(token);

        token = strtok(NULL, " ");
    }

    args[*count] = NULL;
    return args;
}

static void free_args(char** args, int count) {
    for (int i = 0; i < count; i++) {
        free(args[i]);
    }
    free(args);
}

/* ---------- Built-ins ---------- */

static int cmd_echo(char** args, int count) {
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
    }
    perror("pwd");
    return 1;
}

static int cmd_cd(char** args, int count) {
    const char* path = (count > 1) ? args[1] : getenv("HOME");
    if (chdir(path) != 0) {
        fprintf(stderr, "cd: %s: No such file or directory\n", path);
        return 1;
    }
    return 0;
}

static int cmd_export(char** args, int count) {
    if (count != 2) {
        fprintf(stderr, "export: usage: export VARIABLE\n");
        return 1;
    }

    Variable* var = find_variable(args[1]);
    if (!var) {
        fprintf(stderr, "export: %s: variable not found\n", args[1]);
        return 1;
    }

    if (setenv(var->name, var->value, 1) != 0) {
        perror("export");
        return 1;
    }

    return 0;
}

/* ---------- Execution ---------- */

static int run_external(char** args) {
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

    perror("fork");
    return 1;
}

static int execute(char** args, int count) {
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
    else if (strcmp(args[0], "export") == 0) {
        return cmd_export(args, count);
    }

    return run_external(args);
}

/* ---------- Assignment ---------- */

static int handle_assignment(const char* line) {
    const char* eq = strchr(line, '=');
    if (!eq) return 0;

    if (strchr(line, ' ') && strchr(line, ' ') < eq) return 0;

    int len = eq - line;
    if (len == 0) return 0;

    char name[MAX_VAR_LEN];
    strncpy(name, line, len);
    name[len] = '\0';

    const char* value = eq + 1;
    if (*value == '\0') return 0;

    return set_variable(name, value);
}

/* ---------- Entry Point (for tests) ---------- */

int nanoshell_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    char* line = NULL;
    size_t len = 0;
    int status = 0;

    while (1) {
        printf("Nano Shell Prompt > ");
        fflush(stdout);

        if (getline(&line, &len, stdin) == -1) break;

        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n') line[l - 1] = '\0';

        if (line[0] == '\0') continue;

        if (handle_assignment(line)) continue;

        int count = 0;
        char** args = parse_args(line, &count);
        if (!args) continue;

        status = execute(args, count);
        free_args(args, count);
    }

    free(line);
    return status;
}
