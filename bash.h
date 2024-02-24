#include "tokenizer.h"

#include <unistd.h>
#include <sys/wait.h>

void clear_memory(char** args) {
    if (args == NULL) {
        return;
    }
    int i = 0;
    while (args[i] != NULL) {
        free(args[i]);
        ++i;
    }
    free(args);
}

char** get_args(struct Tokenizer* tokenizer) {

    char** args = (char**) malloc((tokenizer->token_count+1) * sizeof(char*));
    if (args == NULL) {
        printf("[ERROR]: can not allocate memory\n");
        return NULL;
    }
    args[tokenizer->token_count] = NULL;

    struct Token* token = tokenizer->head;
    int i = 0;

    while (token != NULL) {
        args[i] = (char*) malloc(token->len+1);
        if (args[i] == NULL) {
            printf("[ERROR]: can not allocate memory\n");
            clear_memory(args);
            return NULL;
        }

        memcpy(args[i], token->start, token->len);
        args[i][token->len] = '\0';

        token = token->next;
        ++i;
    }

    return args;
}

void Exec(struct Tokenizer* tokenizer) {
    if (tokenizer->head == NULL) {
        return;
    }

    char** args = get_args(tokenizer);
    if (args == NULL) {
        return;
    }

    __pid_t pid = fork();
    if (pid < 0) {
        perror("[ERROR]: can not fork\n");

    } else if (pid == 0) {
        int result = execvp(args[0], args);
        if (result < 0) {
            printf("Command not found\n");
        }
    } else {
        int status;
        waitpid(pid, &status, 0);
    }

    clear_memory(args);
}
