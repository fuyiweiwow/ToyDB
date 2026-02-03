#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define BUFFER_SIZE 2048
static char buffer[BUFFER_SIZE];

#ifdef _WIN32
#include <string.h>

char* realine(char* prompt)
{
    fputs(prompt, stdout);
    fgets(buffer, BUFFER_SIZE, stdin);
    char *cpy = malloc(strlen(buffer) + 1);
    strcpy(cpy, buffer);
    cpy[strlen(cpy) - 1] = '\0';
    return cpy;
}

void add_history(char* unused) {}

#else
#include <editline/readline.h>
#include <editline/history.h>
#endif


int main(int argc, char** argv) {

    for (;;)
    {
        char* input = realine("tdb > ");
        add_history(input);

        if(strcmp(input, ".exit") == 0) {
            exit(EXIT_SUCCESS);
        } else {
            printf("Unrecognized command: %s\n", input);
        }
    }
    


}