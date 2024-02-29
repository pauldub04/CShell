#include "tokenizer.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

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
} error;

void print_error_text() {
    switch (error.code) {
    case EC_SYSTEM:
        fprintf(stderr, "[ERROR]: %s\n", error.text);
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

void clear_error() {
    error.code = EC_NONE;
    error.text = NULL;
}


struct Command {
    char** args;
    char* in_file;
    char* out_file;
};

void CommandInit(struct Command* cmd, struct Token* head, size_t tokens_count) {
    cmd->args = NULL;
    cmd->in_file = cmd->out_file = NULL;

    struct Token* token = head;

    size_t infile_cnt = 0;
    size_t outfile_cnt = 0;
    for (size_t i = 0; i < tokens_count; ++i) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            if (token->next == NULL || token->next->type != TT_WORD) {
                error.code = EC_SYNTAX;
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
        error.code = EC_SYNTAX;
        return;
    }

    size_t args_count = tokens_count+1; // beacause of NULL in the end
    token = head;
    for (size_t i = 0; i < tokens_count; ++i) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            char* filepath = token->next->start;
            filepath[token->next->len] = '\0';

            if (token->type == TT_INFILE) {
                cmd->in_file = filepath;
            } else if (token->type == TT_OUTFILE) {
                cmd->out_file = filepath;
            }
            
            args_count -= 2;
        }
        token = token->next;
    }

    cmd->args = (char**) malloc(args_count * sizeof(char*));
    if (cmd->args == NULL) {
        error.code = EC_SYSTEM;
        error.text = "can not allocate memory";
        return;
    }

    token = head;
    size_t arg_number = 0;
    for (size_t i = 0; i < tokens_count; ++i) {
        if (token->type == TT_OUTFILE || token->type == TT_INFILE) {
            token = token->next->next;
            ++i;
            continue;
        }

        cmd->args[arg_number] = token->start;
        cmd->args[arg_number][token->len] = '\0';
        ++arg_number;
        token = token->next;
    }
    cmd->args[args_count-1] = NULL;

}

void CommandFree(struct Command* cmd) {
    if (cmd->args == NULL) {
        return;
    }
    free(cmd->args);
}


void exec_args(char** args) {
    __pid_t pid = fork();
    if (pid < 0) {
        error.code = EC_SYSTEM;
        error.text = strerror(errno);

    } else if (pid == 0) {
        int result = execvp(args[0], args);
        if (result < 0) {
            error.code = EC_COMMAND_NF;
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void exec_args_io(char** args, int io_fd[2]) {
    int dup_stdin_fd = -1;
    if (io_fd[0] != STDIN_FILENO) {
        dup_stdin_fd = dup(STDIN_FILENO);
        dup2(io_fd[0], STDIN_FILENO);
    }
    int dup_stdout_fd = -1;
    if (io_fd[1] != STDOUT_FILENO) {
        dup_stdout_fd = dup(STDOUT_FILENO);
        dup2(io_fd[1], STDOUT_FILENO);
    }

    exec_args(args);

    if (dup_stdin_fd != -1) {
        dup2(dup_stdin_fd, STDIN_FILENO);
        close(dup_stdin_fd);
    }
    if (dup_stdout_fd != -1) {
        dup2(dup_stdout_fd, STDOUT_FILENO);
        close(dup_stdout_fd);
    }
}

// replaces the fd if file is specified
// closes the both fd`s after execution
void exec_command(struct Command* cmd, int io_fd[2]) {
    if (cmd->in_file != NULL) {
        io_fd[0] = open(cmd->in_file, O_RDONLY); 
        if (io_fd[0] < 0) {
            error.code = EC_IO;
            return;
        }
    }
    if (cmd->out_file != NULL) {
        io_fd[1] = open(cmd->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (io_fd[1] < 0) {
            error.code = EC_IO;
            return;
        }
    }

    exec_args_io(cmd->args, io_fd);

    if (io_fd[0] != STDIN_FILENO) {
        close(io_fd[0]);
    }
    if (io_fd[1] != STDOUT_FILENO) {
        close(io_fd[1]);
    }
}


// returns commands number
size_t validate_commands(struct Tokenizer* tokenizer) {
    struct Token* token = tokenizer->head;
    size_t commands_count = 1;
    size_t tokens_count = 0;

    while (token != NULL) {
        if (token->type == TT_PIPE) {
            if (tokens_count == 0) {
                error.code = EC_SYNTAX;
                return 0;
            }
            tokens_count = 0;
            ++commands_count;
        } else {
            ++tokens_count;
        }

        token = token->next;
    }
    return commands_count;
}

struct Command* parse_commands(struct Tokenizer* tokenizer, size_t commands_count) {
    struct Command* commands = (struct Command*) malloc(commands_count * sizeof(struct Token));
    if (commands == NULL) {
        error.code = EC_SYSTEM;
        error.text = "can not allocate memory";
        return NULL;
    }

    struct Token* start_token = tokenizer->head;
    struct Token* token = tokenizer->head;
    size_t i = 0;
    size_t tokens_count = 0;

    while (token != NULL) {
        if (token->type == TT_PIPE) {
            CommandInit(&commands[i], start_token, tokens_count);
            tokens_count = 0;
            start_token = token->next;
            ++i;
        } else {
            ++tokens_count;
        }

        token = token->next;
    }
    
    if (tokens_count != 0) {
        CommandInit(&commands[i], start_token, tokens_count);
    }

    for (i = 0; i < commands_count; ++i) {
        if (commands[i].in_file != NULL && i != 0) {
            error.code = EC_SYNTAX;
            break;
        }
        if (commands[i].out_file != NULL && i != commands_count-1) {
            error.code = EC_SYNTAX;
            break;
        }
    }

    return commands;
}

void process_commands_thru_pipes(struct Command* commands, size_t commands_count) {
    if (commands_count == 1) {
        int io_fd[2] = {STDIN_FILENO, STDOUT_FILENO};
        exec_command(&commands[0], io_fd);
        return;
    }

    if (commands_count == 2) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            error.code = EC_SYSTEM;
            error.text = strerror(errno);
            return;
        }

        int io_fd0[2] = {STDIN_FILENO, pipefd[1]};
        exec_command(&commands[0], io_fd0);
        // close(pipefd[1]);

        if (error.code == EC_NONE) {
            int io_fd1[2] = {pipefd[0], STDOUT_FILENO};
            exec_command(&commands[1], io_fd1);
        }
        
        close(pipefd[0]);
        return;
    }

    // command0 - 1:[pipefd_1]:0 - command1 - 1:[pipefd2]:0 - command2 - ...
    int pipefd_1[2];
    if (pipe(pipefd_1) == -1) {
        error.code = EC_SYSTEM;
        error.text = strerror(errno);
        return;
    }

    int io_fd[2] = {STDIN_FILENO, pipefd_1[1]};
    exec_command(&commands[0], io_fd);
    // close(pipefd_1[1]);

    if (error.code != EC_NONE) {
        close(pipefd_1[0]);
        return;
    }

    int pipefd_2[2];
    for (size_t i = 1; i < commands_count-1; ++i) {
        if (pipe(pipefd_2) == -1) {
            error.code = EC_SYSTEM;
            error.text = strerror(errno);
            close(pipefd_1[0]);
            return;
        }
        io_fd[0] = pipefd_1[0];
        io_fd[1] = pipefd_2[1];

        exec_command(&commands[i], io_fd);
        // close(pipefd_1[0]);
        // close(pipefd_2[1]);

        if (error.code != EC_NONE) {
            close(pipefd_2[0]);
            return;
        }

        pipefd_1[0] = pipefd_2[0];
    }

    io_fd[0] = pipefd_1[0];
    io_fd[1] = STDOUT_FILENO;
    exec_command(&commands[commands_count-1], io_fd);
    // close(pipefd_1[0]);
}


void Exec(struct Tokenizer* tokenizer) {
    if (tokenizer->head == NULL) {
        return;
    }
    clear_error();

    struct Command* commands = NULL;
    size_t commands_count = validate_commands(tokenizer);
    if (error.code == EC_NONE) {
        commands = parse_commands(tokenizer, commands_count);
    }

    if (error.code == EC_NONE) {
        process_commands_thru_pipes(commands, commands_count);
    }
    if (error.code != EC_NONE) {
        print_error_text();
    }


    if (commands == NULL) {
        return;
    }
    for (size_t i = 0; i < commands_count; ++i) {
        CommandFree(&commands[i]);
    }
    free(commands);
}
