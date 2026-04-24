#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

#define INIT_ARGV_CAPACITY 32
#define INPUT_BUFFER_INIT_SIZE 256
#define STDIN_REDIRECTED 0x2
#define STDOUT_REDIRECTED 0x4
#define STDERR_REDIRECTED 0x8

// --- Data Structures ---

typedef struct {
    int argc;
    char **argv;
    int capacity;
} command_t;

typedef struct env_node {
    struct env_node *next;
    char *key;
    char *val;
} env_node_t;

typedef struct {
    env_node_t *local_vars;
    int status;
    int saved_stdin;
    int saved_stdout;
    int saved_stderr;
} shell_state_t;

// --- Tokenizer Functions ---

void command_init(command_t *cmd) {
    cmd->argc = 0;
    cmd->argv = (char **)calloc(INIT_ARGV_CAPACITY, sizeof(char *));
    cmd->capacity = INIT_ARGV_CAPACITY;
}

void command_free_args(command_t *cmd) {
    if (!cmd) return;
    for (int i = 0; i < cmd->argc; ++i) {
        free(cmd->argv[i]);
        cmd->argv[i] = NULL;
    }
    cmd->argc = 0;
}

void command_free_all(command_t *cmd) {
    if (cmd) {
        command_free_args(cmd);
        free(cmd->argv);
        cmd->argv = NULL;
        cmd->capacity = 0;
    }
}

int tokenize_line(char *line, size_t size, command_t *out) {
    if (!line || !out || size == 0) return -1;
    
    char *temp = (char *)malloc(size + 1);
    if (!temp) return -2;

    size_t i = 0;
    while (i < size) {
        size_t temp_len = 0;
        
        // Skip leading whitespace
        while (i < size && (line[i] == ' ' || line[i] == '\t')) i++;
        
        // Read token
        while (i < size && line[i] != ' ' && line[i] != '\t' && line[i] != '\n') {
            temp[temp_len++] = line[i++];
        }
        
        if (temp_len > 0) {
            temp[temp_len] = '\0';
            
            if (out->argc >= out->capacity - 1) {
                out->capacity *= 2;
                char **new_argv = (char **)realloc(out->argv, out->capacity * sizeof(char *));
                if (!new_argv) {
                    free(temp);
                    return -2;
                }
                out->argv = new_argv;
            }
            
            out->argv[out->argc] = strdup(temp);
            if (!out->argv[out->argc]) {
                free(temp);
                return -2;
            }
            out->argc++;
        }
    }
    
    out->argv[out->argc] = NULL;
    free(temp);
    return 0;
}

// --- Environment Variables List ---

env_node_t *env_create_node(const char *key, const char *val) {
    env_node_t *node = (env_node_t *)malloc(sizeof(env_node_t));
    if (node) {
        node->key = key ? strdup(key) : NULL;
        node->val = val ? strdup(val) : NULL;
        node->next = NULL;
    }
    return node;
}

void env_set(env_node_t **head, const char *key, const char *val) {
    if (!head || !key) return;
    
    env_node_t *curr = *head;
    env_node_t *prev = NULL;
    
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            free(curr->val);
            curr->val = strdup(val);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
    
    env_node_t *new_node = env_create_node(key, val);
    if (prev) {
        prev->next = new_node;
    } else {
        *head = new_node;
    }
}

const char *env_get(env_node_t *head, const char *key) {
    while (head) {
        if (strcmp(head->key, key) == 0) return head->val;
        head = head->next;
    }
    return NULL;
}

void env_clear(env_node_t **head) {
    if (!head || !*head) return;
    
    env_node_t *curr = *head;
    while (curr) {
        env_node_t *next = curr->next;
        free(curr->key);
        free(curr->val);
        free(curr);
        curr = next;
    }
    *head = NULL;
}

// --- Execution & Redirection Helpers ---

bool handle_local_assignment(shell_state_t *shell, command_t *cmd) {
    if (!cmd || cmd->argc != 1) return false;
    
    char *equal = strchr(cmd->argv[0], '=');
    if (!equal) return false;
    
    *equal = '\0';
    char *key = cmd->argv[0];
    char *value = equal + 1;
    
    env_set(&shell->local_vars, key, value);
    return true;
}

char *substitute_vars(shell_state_t *shell, const char *str) {
    if (!str) return NULL;
    
    size_t cap = strlen(str) * 2 + 1;
    char *result = (char *)malloc(cap);
    size_t ri = 0;
    
    for (size_t i = 0; str[i] != '\0';) {
        if (str[i] == '$') {
            size_t var_start = i + 1;
            size_t var_len = 0;
            
            while (str[var_start + var_len] && 
                  (isalnum((unsigned char)str[var_start + var_len]) || str[var_start + var_len] == '_')) {
                var_len++;
            }
            
            if (var_len > 0) {
                char varname[256];
                snprintf(varname, sizeof(varname), "%.*s", (int)var_len, str + var_start);
                
                const char *val = env_get(shell->local_vars, varname);
                if (val) {
                    size_t vlen = strlen(val);
                    // Dynamically resize if buffer is too small
                    if (ri + vlen >= cap) {
                        cap = (cap + vlen) * 2;
                        result = (char *)realloc(result, cap);
                    }
                    memcpy(result + ri, val, vlen);
                    ri += vlen;
                }
                i = var_start + var_len;
                continue;
            }
        }
        
        if (ri + 1 >= cap) {
            cap *= 2;
            result = (char *)realloc(result, cap);
        }
        result[ri++] = str[i++];
    }
    
    result[ri] = '\0';
    return result;
}

void replace_args_with_vars(shell_state_t *shell, command_t *cmd) {
    for (int i = 0; i < cmd->argc; ++i) {
        char *new_val = substitute_vars(shell, cmd->argv[i]);
        free(cmd->argv[i]);
        cmd->argv[i] = new_val;
    }
}

int handle_io_redirection(shell_state_t *shell, command_t *cmd) {
    int ret_flags = 0;
    int redirect_start = -1;
    
    for (int i = 0; i < cmd->argc; ++i) {
        if (!cmd->argv[i]) continue;
        
        bool is_in  = (strcmp(cmd->argv[i], "<") == 0);
        bool is_out = (strcmp(cmd->argv[i], ">") == 0);
        bool is_err = (strcmp(cmd->argv[i], "2>") == 0);
        
        if ((is_in || is_out || is_err) && i + 1 < cmd->argc) {
            if (redirect_start == -1) redirect_start = i;
            
            char *filename = substitute_vars(shell, cmd->argv[i + 1]);
            int flags = 0, target_fd = -1;
            
            if (is_in) {
                flags = O_RDONLY;
                target_fd = STDIN_FILENO;
            } else if (is_out) {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                target_fd = STDOUT_FILENO;
            } else {
                flags = O_WRONLY | O_CREAT | O_TRUNC;
                target_fd = STDERR_FILENO;
            }
            
            int new_fd = open(filename, flags, 0666);
            if (new_fd < 0) {
                // Corrected to match exact required error strings
                if (is_in) {
                    fprintf(stderr, "cannot access %s: %s\n", filename, strerror(errno));
                } else {
                    fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                }
                free(filename);
                return -1;
            }
            
            dup2(new_fd, target_fd);
            close(new_fd);
            
            if (target_fd == STDIN_FILENO)  ret_flags |= STDIN_REDIRECTED;
            if (target_fd == STDOUT_FILENO) ret_flags |= STDOUT_REDIRECTED;
            if (target_fd == STDERR_FILENO) ret_flags |= STDERR_REDIRECTED;
            
            free(filename);
            i++; // Skip the filename token
        }
    }
    
    if (redirect_start != -1) {
        // Truncate argv to hide redirection operators from the command
        for (int i = redirect_start; i < cmd->argc; i++) {
            free(cmd->argv[i]);
            cmd->argv[i] = NULL;
        }
        cmd->argc = redirect_start;
    }
    
    return ret_flags;
}

void restore_io(shell_state_t *shell, int flags) {
    if (flags & STDIN_REDIRECTED)  dup2(shell->saved_stdin, STDIN_FILENO);
    if (flags & STDOUT_REDIRECTED) dup2(shell->saved_stdout, STDOUT_FILENO);
    if (flags & STDERR_REDIRECTED) dup2(shell->saved_stderr, STDERR_FILENO);
}

// --- Execution Core ---

static int execute_builtin(shell_state_t *shell, command_t *cmd) {
    if (!cmd || !cmd->argv[0]) return -1;
    
    if (strcmp("exit", cmd->argv[0]) == 0) return 1;
    
    if (strcmp("pwd", cmd->argv[0]) == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) printf("%s\n", cwd);
        shell->status = 0;
        return 0;
    }
    
    if (strcmp("echo", cmd->argv[0]) == 0) {
        for (int i = 1; i < cmd->argc; i++) {
            printf("%s%s", cmd->argv[i], (i < cmd->argc - 1) ? " " : "");
        }
        printf("\n");
        shell->status = 0;
        return 0;
    }
    
    if (strcmp("cd", cmd->argv[0]) == 0) {
        const char *path = cmd->argc > 1 ? cmd->argv[1] : getenv("HOME");
        shell->status = (chdir(path) != 0) ? 1 : 0;
        if (shell->status != 0) perror("cd");
        return 0;
    }
    
    if (strcmp("export", cmd->argv[0]) == 0 && cmd->argc == 2) {
        char *equal = strchr(cmd->argv[1], '=');
        if (equal) {
            *equal = '\0';
            setenv(cmd->argv[1], equal + 1, 1);
        }
        shell->status = 0;
        return 0;
    }
    
    return -1; // Not a builtin
}

static void execute_binary(shell_state_t *shell, command_t *cmd) {
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork");
        shell->status = 1;
        return;
    }
    
    if (pid == 0) {
        execvp(cmd->argv[0], cmd->argv);
        perror("execvp");
        _exit(errno == ENOENT ? 127 : 126);
    } else {
        int wstatus;
        waitpid(pid, &wstatus, 0);
        shell->status = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 1;
    }
}

int process_command(shell_state_t *shell, command_t *cmd) {
    if (handle_local_assignment(shell, cmd)) return 0;
    
    replace_args_with_vars(shell, cmd);
    if (cmd->argc == 0) return 0;
    
    int redir_flags = handle_io_redirection(shell, cmd);
    if (redir_flags == -1) {
        shell->status = 1;
        return 0;
    }
    
    int is_exit = execute_builtin(shell, cmd);
    if (is_exit == 1) return 1;
    
    if (is_exit == -1) {
        execute_binary(shell, cmd);
    }
    
    restore_io(shell, redir_flags);
    return 0;
}

// --- Main Entry Function Called By Tests ---

int microshell_main(int argc, char **argv) {
    (void)argc; 
    (void)argv;
    
    shell_state_t shell = {
        .local_vars = NULL,
        .status = 0,
        .saved_stdin = dup(STDIN_FILENO),
        .saved_stdout = dup(STDOUT_FILENO),
        .saved_stderr = dup(STDERR_FILENO)
    };
    
    command_t cmd;
    command_init(&cmd);
    
    char *input = NULL;
    size_t input_size = 0;
    
    while (1) {
        fflush(stdout);
        
        ssize_t bytes_read = getline(&input, &input_size, stdin);
        if (bytes_read == -1) break;
        
        // Strip trailing newline
        if (bytes_read > 0 && input[bytes_read - 1] == '\n') {
            input[bytes_read - 1] = '\0';
        }
        
        if (input[0] == '\0') continue;
        
        if (tokenize_line(input, strlen(input), &cmd) == 0) {
            if (process_command(&shell, &cmd) == 1) break;
        } else {
            fprintf(stderr, "Error parsing command\n");
            shell.status = 1;
        }
        
        command_free_args(&cmd);
    }
    
    // Cleanup
    free(input);
    command_free_all(&cmd);
    env_clear(&shell.local_vars);
    
    close(shell.saved_stdin);
    close(shell.saved_stdout);
    close(shell.saved_stderr);
    
    return shell.status;
}
