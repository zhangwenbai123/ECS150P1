all: exec

exec: sshell.c
	gcc -g -Wall -Wextra -Werror sshell.c -o sshell

clean:
	rm -f sshell
