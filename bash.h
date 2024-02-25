#include "tokenizer.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

enum ErrorCode {
    EC_NONE = 0,
    EC_SYSTEM,
    EC_COMMAND_NF,
    EC_SYNTAX,
    EC_IO,
};

struct Error {
    enum ErrorCode code;
    char* text;
};

struct CommandOptions {
    char** args;
    size_t args_len;
    
    int in_fd;
    int out_fd;

    struct Error error;
};

void CommandOptionsInit(struct CommandOptions* cmd_options, const struct Tokenizer* tokenizer) {
    cmd_options->args_len = tokenizer->token_count+1; // beacause of NULL in the end
    cmd_options->in_fd = STDIN_FILENO;
    cmd_options->out_fd = STDOUT_FILENO;

    struct Token* token = tokenizer->head;

    int infile_cnt = 0;
    int outfile_cnt = 0;
    while (token != NULL) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            if (token->next == NULL || token->next->type != TT_WORD) {
                cmd_options->error.code = EC_SYNTAX;
                return;
            }
            if (token->type == TT_INFILE) {
                ++infile_cnt;
            } else if (token->type == TT_OUTFILE) {
                ++outfile_cnt;
            }
        }
        token = token->next;
    }
    if (infile_cnt > 1 || outfile_cnt > 1) {
        cmd_options->error.code = EC_SYNTAX;
        return;
    }

    token = tokenizer->head;
    while (token != NULL) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            char* filepath = strndup(token->next->start, token->next->len);
            if (filepath == NULL) {
                cmd_options->error.code = EC_SYSTEM;
                cmd_options->error.text = strerror(errno);
                return;
            }

            if (token->type == TT_INFILE) {
                cmd_options->in_fd = open(filepath, O_RDONLY); 
                if (cmd_options->in_fd < 0) {
                    cmd_options->error.code = EC_IO;
                    free(filepath);
                    return;
                }
            } else if (token->type == TT_OUTFILE) {
                cmd_options->out_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666); 
                if (cmd_options->out_fd < 0) {
                    cmd_options->error.code = EC_IO;
                    free(filepath);
                    return;
                }
            }
            free(filepath);
            
            cmd_options->args_len -= 2;
        }
        token = token->next;
    }

    cmd_options->args = (char**) malloc(cmd_options->args_len * sizeof(char*));
    if (cmd_options->args == NULL) {
        cmd_options->error.code = EC_SYSTEM;
        cmd_options->error.text = "can not allocate memory";
        return;
    }

    token = tokenizer->head;
    int i = 0;
    while (token != NULL) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            token = token->next->next;
            continue;
        }

        cmd_options->args[i] = strndup(token->start, token->len);
        if (cmd_options->args[i] == NULL) {
            cmd_options->error.code = EC_SYSTEM;
            cmd_options->error.text = strerror(errno);
            return;
        }

        token = token->next;
        ++i;
    }
    cmd_options->args[cmd_options->args_len-1] = NULL;

}

void CommandOptionsFree(struct CommandOptions* cmd_options) {
    if (cmd_options->in_fd != STDIN_FILENO) {
        close(cmd_options->in_fd);
    }
    if (cmd_options->out_fd != STDOUT_FILENO) {
        close(cmd_options->out_fd);
    }

    if (cmd_options->args == NULL) {
        return;
    }
    for (int i = 0; i < cmd_options->args_len; ++i) {
        if (cmd_options->args[i] != NULL) {
            free(cmd_options->args[i]);
        }
    }
    free(cmd_options->args);
}


void print_error(const struct Error* error) {
    switch (error->code) {
    case EC_SYSTEM:
        fprintf(stderr, "[ERROR]: %s\n", error->text);
        break;
    case EC_COMMAND_NF:
        fprintf(stdout, "Command not found\n");
        break;
    case EC_SYNTAX:
        fprintf(stdout, "Syntax error\n");
        break;
    case EC_IO:
        fprintf(stdout, "I/O error\n");
        break;
    default:
        break;
    }
}

void exec_command(struct CommandOptions* cmd_options) {
    __pid_t pid = fork();
    if (pid < 0) {
        cmd_options->error.code = EC_SYSTEM;
        cmd_options->error.text = strerror(errno);

    } else if (pid == 0) {
        int result = execvp(cmd_options->args[0], cmd_options->args);
        if (result < 0) {
            cmd_options->error.code = EC_COMMAND_NF;
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void process_command(struct CommandOptions* cmd_options) {
    int dup_stdin_fd = -1;
    if (cmd_options->in_fd != STDIN_FILENO) {
        dup_stdin_fd = dup(STDIN_FILENO);
        dup2(cmd_options->in_fd, STDIN_FILENO);
    }
    int dup_stdout_fd = -1;
    if (cmd_options->out_fd != STDOUT_FILENO) {
        dup_stdout_fd = dup(STDOUT_FILENO);
        dup2(cmd_options->out_fd, STDOUT_FILENO);
    }

    exec_command(cmd_options);

    if (dup_stdin_fd != -1) {
        dup2(dup_stdin_fd, STDIN_FILENO);
        close(dup_stdin_fd);
    }
    if (dup_stdout_fd != -1) {
        dup2(dup_stdout_fd, STDOUT_FILENO);
        close(dup_stdout_fd);
    }
}


void Exec(struct Tokenizer* tokenizer) {
    if (tokenizer->head == NULL) {
        return;
    }

    struct CommandOptions cmd_options;
    CommandOptionsInit(&cmd_options, tokenizer);

    if (cmd_options.error.code == EC_NONE) {
        process_command(&cmd_options);
    }

    if (cmd_options.error.code != EC_NONE) {
        print_error(&cmd_options.error);
    }

    CommandOptionsFree(&cmd_options);
}
