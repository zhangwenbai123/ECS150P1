#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include<fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>

#define CMDLINE_MAX 512
#define PIPELINE_MAX 15   //The prompt says the maximum pipe instructions is 4, let's call it 15 here.
#define ARGUMENT_MAX 16
#define REDI_PATH_MAX 100

//data structure definition.
typedef struct single_cmd {
        char *args[ARGUMENT_MAX + 1];
        int numArgs;
        bool isRedirect;
        bool isAppend;
        char redirectLoc[REDI_PATH_MAX];
} single_cmd;

//The pipeline contains array of commands that are seperated by '|'
typedef struct pipeline {
        single_cmd cmds[PIPELINE_MAX];
        int numPipeInstructions;
        pid_t pid[PIPELINE_MAX];
} pipeline;

enum RedirectState{LookForRedi, Read, LookForFirstLetter, Start};
enum RedirectError{MissingCommand, NoOutput, NoError};



//function declarations.
enum RedirectError redirection (char* input, single_cmd* cmd);
int parsePipeLine(char* input, char** pipelineParse);
bool isLineWhitespace(char* input);




int main(void)
{
        char input[CMDLINE_MAX];
        char inputCopy[CMDLINE_MAX];
        pipeline pipeOfCmds;

        char *pipelineParse[PIPELINE_MAX];  //Seperate each command string with '|' symbol.
        int returnStatus[PIPELINE_MAX];  //The exit statues from each child.
        int numPipeInstructions;    //how many instructions that has been seperated by '|' symbol.

        while (1) {
                char *nl;
                strcpy(input, "");

                /* Print prompt */
                printf("sshell$ ");
                fflush(stdout);

                /* Get command line */
                fgets(input, CMDLINE_MAX, stdin);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", input);
                        fflush(stdout);
                }

                if (isLineWhitespace(input)) {
                        continue;
                }

                /* Remove trailing newline from command line */
                nl = strchr(input, '\n');
                if (nl) {
                        *nl = '\0';
                }

                //get a copy of the
                strcpy(inputCopy, input);

                //Seperate the pipeline string by '|'
                numPipeInstructions = parsePipeLine(input, pipelineParse);
                pipeOfCmds.numPipeInstructions = numPipeInstructions;

                //Parse each pipe instructions into individual commmand arguments.
                bool is_parse_error = false;
                for (int i = 0; i < numPipeInstructions; ++i) {
                        //Initiate data in each command
                        pipeOfCmds.cmds[i].isRedirect = false;
                        pipeOfCmds.cmds[i].numArgs = 0;

                        //Check if it's empty which means missing command.
                        if (isLineWhitespace(pipelineParse[i])) {
                                fprintf(stderr, "Error: missing command\n");
                                is_parse_error = true;
                                break;
                        }
                        //CheckRedirection---------------------------------------------------------------
                        enum RedirectError errorType = redirection(pipelineParse[i], &(pipeOfCmds.cmds[i]));
                        if (errorType == MissingCommand) {
                                fprintf(stderr, "Error: missing command\n");
                                is_parse_error = true;
                                break;
                        }
                        if (errorType == NoOutput) {
                                fprintf(stderr, "Error: no output file\n");
                                is_parse_error = true;
                                break;
                        }
                        if (pipeOfCmds.cmds[i].isRedirect && i != numPipeInstructions-1) {
                                fprintf(stderr, "Error: mislocated output redirection\n");
                                is_parse_error = true;
                                break;
                        }
                        //-------------------------------------------------------------------------------



                        //Parse--------------------------------------------------------------------------

                        char *token;
                        // get the first token
                        token = strtok(pipelineParse[i], " ");

                        // walk through other tokens
                        int argIndex = 0;
                        while( token != NULL) {
                                if (argIndex == 16) {
                                        fprintf(stderr, "Error: too many process arguments\n");
                                        is_parse_error = true;
                                        break;
                                }
                                pipeOfCmds.cmds[i].args[argIndex] = token;
                                token = strtok(NULL, " ");
                                argIndex++;
                        }
                        pipeOfCmds.cmds[i].args[argIndex] = NULL;
                        pipeOfCmds.cmds[i].numArgs = argIndex;

                }

                if (is_parse_error) {
                        continue;
                }


                //-------------------------------------------------------------------------------

                //int i = 0;



                // Builtin command
                if (!strcmp(pipeOfCmds.cmds[0].args[0], "exit")) {
                        if (!fork()) { //Children try to exit.
                                fprintf(stderr, "Bye...\n");
                                break;
                        } else {// Parent
                                int retval;
                                waitpid(-1, &retval, 0);       // Wait for child to exit
                                fprintf(stderr, "+ completed '%s' [%d]\n", inputCopy, WEXITSTATUS(retval));
                                break;
                        }
                }

                if (!strcmp(pipeOfCmds.cmds[0].args[0], "cd")) {

                        if (!fork()) {
                                if (chdir(pipeOfCmds.cmds[0].args[1]) == 0) {
                                        exit(0);
                                } else {
                                        fprintf(stderr, "Error: cannot cd into directory\n");
                                        exit(1);
                                }

                        } else {// Parent
                                int retval;
                                waitpid(-1, &retval, 0);       // Wait for child to exit
                                if (retval == 0) chdir(pipeOfCmds.cmds[0].args[1]);
                                fprintf(stdout, "+ completed '%s' [%d]\n", inputCopy, WEXITSTATUS(retval));
                                continue;
                        }
                }

                int fd = 0;
                int pipefd[2*(pipeOfCmds.numPipeInstructions-1)];

                if (pipeOfCmds.numPipeInstructions > 1) {
                        //Create a pipe every two spot in the array for all the processes.
                        for (int i = 0; i < pipeOfCmds.numPipeInstructions-1; ++i) {
                                //The location of the corresponding pipe is the original address plus 2*i.
                                pipe(pipefd + 2*i);
                        }
                }

                for (int i = 0; i < pipeOfCmds.numPipeInstructions; ++i) {
                        //If it is a redirect, check if that file is able to be opened or not.
                        if (pipeOfCmds.cmds[i].isRedirect || pipeOfCmds.cmds[i].isAppend) {
                                if (pipeOfCmds.cmds[i].isRedirect) {
                                        fd = open(pipeOfCmds.cmds[i].redirectLoc, O_TRUNC | O_WRONLY | O_CREAT,0644);
                                        if (fd < 0) {
                                                fprintf(stderr, "Error: cannot open output file\n");
                                                continue;
                                        }
                                } else {
                                        fd = open(pipeOfCmds.cmds[i].redirectLoc, O_APPEND | O_RDWR | O_CREAT,0644);
                                        if (fd < 0) {
                                                fprintf(stderr, "Error: cannot open output file\n");
                                                continue;
                                        }
                                }
                        }

                        //Fork for a new process to execute.
                        pid_t pid = fork();

                        //Child process to execute
                        if (pid == 0) {
                                //Close all the unecessary pipes for other processes to have it work.
                                for (int j = 0; j < 2*(pipeOfCmds.numPipeInstructions-1); ++j) {
                                        if (j != 2*(i-1) && j != (2*i + 1)) {
                                                close(pipefd[j]);
                                        }
                                }

                                //Not the first process, then need to Replace stdin with Read Only connection.
                                if (i != 0) {
                                        // Replace stdin with pipe
                                        dup2(pipefd[2*(i-1)], STDIN_FILENO);
                                        // Close now unused FD
                                        close(pipefd[2*(i-1)]);
                                }

                                //Not the last process, redirect the output to Write Only connection.
                                if (i != pipeOfCmds.numPipeInstructions-1) {
                                        // Replace stdout with pipe
                                        dup2(pipefd[2*i + 1], STDOUT_FILENO);
                                        // Close now unused FD
                                        close(pipefd[2*i + 1]);
                                } else {//The last process, check redirection.
                                        if (pipeOfCmds.cmds[i].isRedirect || pipeOfCmds.cmds[i].isAppend) {
                                                dup2(fd, STDOUT_FILENO);
                                        }
                                }

                                if (!strcmp(pipeOfCmds.cmds[i].args[0], "pwd")) {
                                        char cwd[REDI_PATH_MAX];

                                        if (getcwd(cwd, sizeof(cwd)) == NULL) {
                                                fprintf(stderr, "PWD Error\n");
                                                exit(1);
                                        } else {
                                                fprintf(stdout, "%s\n", cwd);
                                                exit(0);
                                        }
                                } else if (!strcmp(pipeOfCmds.cmds[i].args[0], "sls")) {
                                        char cwd[REDI_PATH_MAX];
                                        struct dirent **filelist;
                                        struct stat sb;
                                        int num_of_file;

                                        if (getcwd(cwd, sizeof(cwd)) == NULL) {//get the name of the current working directory
                                                fprintf(stderr, "cannot open directory\n");
                                                exit(1);
                                        }
                                        num_of_file = scandir(cwd, &filelist, NULL, NULL); //get the list of name of files in current directory


                                        for(int i = 0; i < num_of_file; i++) { // print the name and size of each file in terminal
                                                stat(filelist[i]->d_name, &sb);
                                                if((sb.st_mode & S_IFMT) != S_IFREG) {
                                                        continue;
                                                }
                                                printf("%s (%lld bytes)\n", filelist[i]->d_name, (long long) sb.st_size);
                                        }

                                        for(int i = 0; i < num_of_file; i++) { //free the space created by scandir()
                                                free(filelist[i]);
                                        }
                                        free(filelist);
                                        exit(0);

                                }else { //Regular command.
                                        execvp(pipeOfCmds.cmds[i].args[0], pipeOfCmds.cmds[i].args);     // Execute command
                                        perror("fail to execute");          // Coming back here is an error
                                        exit(1);
                                }

                        }
                        pipeOfCmds.pid[i] = pid;
                }

                int retval;

                for (int j = 0; j < 2*(pipeOfCmds.numPipeInstructions-1); ++j) {
                        close(pipefd[j]);
                }

                for (int i = 0; i < pipeOfCmds.numPipeInstructions; ++i) {
                        waitpid(pipeOfCmds.pid[i], &retval, 0);       // Wait for child to exit
                        returnStatus[i] = retval;
                }


                fprintf(stdout, "+ completed '%s' ", inputCopy);
                for (int i = 0; i < pipeOfCmds.numPipeInstructions; ++i) {
                        fprintf(stdout, "[%d]", WEXITSTATUS(returnStatus[i]));
                }
                fprintf(stdout, "\n");



        }

        return EXIT_SUCCESS;
}


enum RedirectError redirection (char* input, single_cmd* cmd)
{
        enum RedirectState state = Start;

        int len = strlen(input);
        int letterCount;

        for (int i = 0; i < len; ++i) {
                if (state == LookForRedi) {
                        if (input[i] == '>') {
                                input[i] = ' ';
                                state = LookForFirstLetter;
                                letterCount = 0;
                                cmd->isRedirect = true;
                                cmd->isAppend = false;
                                if ((i+1 < len) && (input[i+1] == '>')) {
                                        cmd->isRedirect = false;
                                        cmd->isAppend = true;
                                        input[i+1] = ' ';
                                }
                        }
                } else if (state == LookForFirstLetter) {
                        if (isalpha(input[i]) || isdigit(input[i]) || input[i]=='.') {
                                state = Read;
                                cmd->redirectLoc[letterCount] = input[i];
                                ++letterCount;
                                input[i] = ' ';
                        }
                } else if (state == Read) {
                        if (isspace(input[i])) {
                                cmd->redirectLoc[letterCount] = '\0';
                                state = LookForRedi;
                        } else {
                                cmd->redirectLoc[letterCount] = input[i];
                                ++letterCount;
                                input[i] = ' ';
                        }
                } else {
                        if (input[i] == '>') {
                                return MissingCommand;
                        } else if (!isspace(input[i])) {
                                state = LookForRedi;
                        }
                }
        }

        if (state == LookForFirstLetter) {
                return NoOutput;
        }

        if (state == Read) {
                cmd->redirectLoc[letterCount] = '\0';
        }

        return NoError;

}

int parsePipeLine(char* input, char** pipelineParse) {
        int len = strlen(input);
        int pipeCount = 0;

        pipelineParse[pipeCount] = input;
        pipeCount++;

        for (int i = 0; i < len; ++i) {
                if (input[i] == '|') {
                        input[i] = '\0';
                        pipelineParse[pipeCount] = input + i + 1;
                        pipeCount++;
                }
        }

        return pipeCount;
}


bool isLineWhitespace(char* input)
{
        int len = strlen(input);

        for (int i = 0; i < len; ++i) {
                if (!isspace(input[i])) {
                        return false;
                }
        }

        return true;
}
