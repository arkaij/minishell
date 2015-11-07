#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <ctype.h>

#define INIT 1
#define IS_PATH 2
#define IS_REDIR 3
#define IS_ARG 4
#define IS_END 5

#define IS_DEBUG 0

#if IS_DEBUG == 1
#define DEBUG(fmt, arg...) fprintf(stderr, fmt, ##arg)
#else
#define DEBUG(fmt, arg...)
#endif

struct Redir {
        int flags;
	char *fd;
	char *flag;
	char *filename;
};

struct Command {
        char *path;
        int argc;
        char *argv[32];
        struct Redir redir;
        int pipe;
};


int
isflag(char *s)
{
	if (strstr(s, ">") != NULL || strstr(s, ">>") != NULL || strstr(s, "<") != NULL ) {
		return 1;
	} else {
		return 0;
	}
}


int get_lexems(void);
void fill_commands(void);
void print_cmds(void);
void redirection(struct Command *);
void fork1(int *);
void my_dup(int *, int);
int dopipe(struct Command *);
void execute(void);


char lexems[32][128];
int lexems_count;
int cmds_count;
struct Command cmds[32];


int main(int argc, char *argv[])
{
	int iseof;

	do {
		printf("minishell:$ ");

		iseof = get_lexems();

		fill_commands();
		
		print_cmds();
		
		execute();
		
		DEBUG("cmds count = %d\n", cmds_count);
	} while (iseof == 0);

        return 0;
}


int
get_lexems(void)
{
	int i, j;
	char c;
	
	memset(lexems, 0, sizeof(lexems));

	i = 0;
	j = 0;
	while ((c = getchar()) != '\n' && c != EOF) {
		DEBUG("get lexem\n");

		if (isblank(c)) { /* skip tabs and spaces */
			continue;
		}

		while (isgraph(c)) { /* get lexem - sequence of printable characters */
			
			if (c == '|' || c == ';') { /* processing of separators */
				if (j != 0) i++;
				j = 0;
				lexems[i][j++] = c;
				break;

			} else if (c == '>' || c == '<') { /* reading redirection */
				if (j != 0) i++;
				j = 0;
				lexems[i][j++] = c;
				c = getchar();
				if (c == '>') {
					lexems[i][j++] = c;
				} else {
					ungetc(c, stdin);
				}
				break;

			} else { /* processing of other characters */
				lexems[i][j++] = c;
			}
			c = getchar();
		}
		lexems[i][j] = '\0';
		i++;
		j = 0;

		if (c == '\n' || c == EOF) break;
	}

	lexems_count = i;

	for (i = 0; i < lexems_count; i++) {
		DEBUG("%d %s\n", i, lexems[i]);
	}

	if (c == EOF) {
		printf("\n");
		return 1;
	}

	return 0;
}



void fill_commands(void)
{
        struct Command *cmd;
        int i, cond;

	cmd = cmds;
	memset(cmd, 0, sizeof(cmds));

        cmds_count = 0;
        cond = INIT;
        for (i = 0; i < lexems_count; i++) {
                switch (cond) {
                        case INIT:
                                cmd->pipe = 0;
                                cond = IS_PATH;

                        case IS_PATH:
				cmd->path = lexems[i];
				cmd->argv[0] = cmd->path;
                                cmd->argc = 1;

                                if (isflag(lexems[i+1]) || (isdigit(lexems[i+1][0]) && isflag(lexems[i+2]))) {
                                        cond = IS_REDIR;

                                } else if (strcmp(lexems[i+1], "|") == 0 || strcmp(lexems[i+1], ";") == 0 || i == lexems_count-1) {
                                        cond = IS_END; 
                                } else {
                                        cond = IS_ARG;
                                }

                                cmds_count++;
                                
                                continue;

                        case IS_REDIR:
				if (isdigit(lexems[i][0])) {
					cmd->redir.fd = lexems[i];
					cmd->redir.flag = lexems[++i];
					cmd->redir.filename = lexems[++i];
				} else {
					cmd->redir.flag = lexems[i];
					cmd->redir.filename = lexems[++i];
				}

                                if (strcmp(lexems[i+1], "|") == 0 || strcmp(lexems[i+1], ";") == 0 || i == lexems_count-1) {
                                        cond = IS_END;
                                } else {
                                        cond = IS_ARG;
                                }

                                continue;

                        case IS_ARG:
                                cmd->argv[cmd->argc++] = lexems[i];
                                
                                if (isflag(lexems[i+1]) || (isdigit(lexems[i+1][0]) && isflag(lexems[i+2]))) {
                                        cond = IS_REDIR;

                                } else if (strcmp(lexems[i+1], "|") == 0 || strcmp(lexems[i+1], ";") == 0 || i == lexems_count-1) {
                                        cond = IS_END;

                                } else {
                                        cond = IS_ARG;
                                }
                                
                                continue;

                        case IS_END:
                                if (strcmp(lexems[i], ";") == 0) {
                                        cmd++;
                                        
                                } else if (strcmp(lexems[i], "|") == 0) {
                                        cmd->pipe = 1;
                                        cmd++;
                                        
                                } else {
                                        fprintf(stderr, "Unknown command: %s\n", lexems[i]);
                                }

                                cond = IS_PATH;

                                continue;
                }
        }
}


void
print_cmds(void)
{
#if IS_DEBUG != 0
	int i, j;

	for (i = 0; i < cmds_count; i++) {
		printf("cmd[%d]:\n\tpath: %s\n\targc = %d\n\targv:\n", i, cmds[i].path, cmds[i].argc);
		for (j = 1; j < cmds[i].argc; j++) {
			printf("\t\t%s\n", cmds[i].argv[j]);
		}
		printf("\tredir: %s %s %s\n", cmds[i].redir.fd, cmds[i].redir.flag, cmds[i].redir.filename);
		printf("\tpipe = %d\n", cmds[i].pipe);
	}
#endif
}


void
redirection(struct Command *cmd)
{
	int fd;

	if (cmd->redir.flag == NULL || cmd->redir.filename == NULL) return;
	
	if (strstr(cmd->redir.flag, ">>") != NULL) {
                cmd->redir.flags |= O_CREAT | O_APPEND | O_WRONLY;
                if (cmd->redir.fd != NULL && atoi(cmd->redir.fd) != 0) {
                        close(atoi(cmd->redir.fd));
                } else {
                	close(1);
                }

	} else if (strstr(cmd->redir.flag, ">") != NULL) {
                cmd->redir.flags |= O_CREAT | O_TRUNC | O_WRONLY ;
                if (cmd->redir.fd != NULL && atoi(cmd->redir.fd) != 0) {
                        close(atoi(cmd->redir.fd));
                } else {
                        close(1);
                }

        } else if (strstr(cmd->redir.flag, "<") != NULL ) {
                cmd->redir.flags |= O_RDONLY;
                close(0);

        } else {
                fprintf(stderr, "unknown flag %s\n", cmd->redir.flag);
                exit(1);
        }
	
	fd = open(cmd->redir.filename, cmd->redir.flags, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd == -1) {
		fprintf(stderr, "can't open file %s\n", cmd->redir.filename);
		exit(1);
	}
}


void
fork1(int *pid)
{
	if ((*pid = fork()) == -1) {
		perror("fork");
		exit(1);
	}
}


void
my_dup(int io[2], int fd)
{
	dup2(io[fd], fd);
	close(io[0]);
	close(io[1]);
}


int
dopipe(struct Command *cmd)
{
	int pid, io[2], i;

	fork1(&pid);

	if (pid == 0) {
		for (;cmd->path != NULL; cmd++) {
			if (cmd->pipe) {
				if (pipe(io) != 0) {
					perror("pipe error\n");
				       	exit(1);
				}

				fork1(&pid);

				if (pid == 0) {
					DEBUG("child %s\n", cmd->path);
					
					my_dup(io, 1);
					
					redirection(cmd);
					
					if (execvp(cmd->path, cmd->argv) == -1) {
						fprintf(stderr, "minishell: %s: command not found\n", cmd->path);
						exit(1);
					}
				} else {
					my_dup(io, 0);
					continue;
				}
			}

			redirection(cmd);
			
			if (execvp(cmd->path, cmd->argv) == -1) {
				fprintf(stderr, "minishell: %s: command not found\n", cmd->path);
				exit(1);
			}
		}
	}
	
	while (wait(NULL) > 0);

	DEBUG("cmd - %s\n", cmd->path);	
	for (i = 0; cmd->path == NULL || cmd->pipe != 0; cmd++, i++) {
		DEBUG("CMD_PATH %s is_pipe %d\n", cmd->path, cmd->pipe);

	}
	DEBUG("pid = %d; i = %d\n", pid, i);

	return i;
}


void
execute(void)
{
	int pid, i;
	
	for (i = 0; i < cmds_count; i++) {
		if (cmds[i].pipe == 1) { /* pipe stuff */
			i += dopipe(&cmds[i]);
			DEBUG("cmds->path - %s\n", cmds[i].path);
		} else {
			DEBUG("Execute independent\n");
			
			fork1(&pid);
			
			if (pid == 0) {
				DEBUG("flag - %s; filename - %s\n", cmds[i].redir.flag, cmds[i].redir.filename);
				
				redirection(&cmds[i]);
				
				if (execvp(cmds[i].path, cmds[i].argv) == -1) {
					fprintf(stderr, "minishell: %s: command not found\n", cmds[i].path);
					exit(1);
				}
			}

			wait(NULL);
		}
	}
}
