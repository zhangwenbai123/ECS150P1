# Report For sshell.c

## Wenbai Zhang & Wenjie Li

The first step is to make sure the fork() and watpid() function works. So if the command is not builtin command, we use fork() to create a child to run execvp() for execution and let the shell wait for results.

For builtin commmand `cd` and `pwd`, there is a little difference here is that `pwd` does its job in a child process using getcwd() and printf(), but `cd` has its child to check if cd is working, then let the parent change the directory by using chdir().


## Parsing and Data Structure

The whole parsing logic is : Seperate pipeline commands by '|' --> Readout output redirection --> Parse the arguments seperated by space.

Before all this, the parsing is done using strtok() to break that single command into tokens seperated by ' ' and save it into our own datastructure `single_cmd`
```c
typedef struct single_cmd {
    char *args[ARGUMENT_MAX + 1];
    int numArgs;
    bool isRedirect;
    bool isAppend;
    char redirectLoc[REDI_PATH_MAX];
} single_cmd;
```
It has the information we needed such as the array of token pointers, number of arguments, is it redirecting in the end or appending, and the redirection location.

For checking redirection, We created our own redirection function called `enum RedirectError redirection (char* input, single_cmd* cmd)`. It returns a enum indicating what type of error if it has one, such as missing command or no output file. Then, since we tested in our terminal that redirection can be placed in any place in a command line like `date >out.txt -u`, we came up with an algorithm that locates '>' symbol in side the input command, read the text after it until a white space, and replace every character we read to a space for easy arguments parsing. For example, `date >out.txt -u` will become `date (Space x 10) -u`. Then we parse it into our command struct.

For seperating the commands by '|', we create a data structure:
```c
typedef struct pipeline {
        single_cmd cmds[PIPELINE_MAX];
        int numPipeInstructions;
        pid_t pid[PIPELINE_MAX];
} pipeline;

```
It is has an array of commands that we just created, and it has a list of pid to store all the working children's pid. To seperate all the commands, we replace all the '|' character with a NULL character in the input to seperate it into different strings, and parse the redirection and arguments by passing into the previous parsing functions we have.


## Redirection

For output redirection, we check the boolean if it has redirection, then we open the file according to its file name, finnaly, in the executing child process, we change the standard output file descriptor row, which has a fd number 1, to the information of our output file by using `dup2(fd, STDOUT_FILENO);`, and we close fd since its already been duplicated in the STDOUT row.

## Piping
The main logic for piping is that we let the parent (the shell) to run a for loop to create n children in the pipeline. We use the parent to create n-1 pipes which are all stored in a integer array with 2*(n-1) items, and each adjcent two array location forms a pipe.

Inside the child, it is divided into cases such as the first child, the last child and the in-between ones. If it is the first child, it closes the pipe Read connector since it's in the beginning and connects the standard out to pipe Write connector using `dup2(pipefd[1], STDOUT_FILENO);` and `close(pipefd[1]);`. The number `1` is just and example, in the real code we calculate the location of that particular pipe in pipe array containing all the pipes. If it is the last child, we close the Write connector and connect the STDIN with the last pipe Read only connector using `dup2(pipefd[0], STDIN_FILENO);`.

Lastly, the parent exit the for loop and wait for all children by using another forloop to wait each individual children by PID and get the return value for all of them.

## Extra Features.
SLS: sls is a command that display name and size of regular files, so we need to find a way to access the datarelated to each file. So first we get the name of the file, then we used `scandir()` function to get an array of names of files. By using the array of names, we can use `stat()` data structure discussed in lecture to get information about each file. Since we only want to print the information related to regular file, we used `sb.st_mode & S_IFMT` to check whether we find a regular file, directory or other files. After doing these steps, we can print data we collected on the terminal.

Appending: For appending into the end of the output file, we just have to change the open flags by removing `O_TRUNC` flag, and change it to `O_APPEND`.
