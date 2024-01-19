/*
   CS374 Assignment: Smallsh
   Author: Maxwell Cole
   Date: November 15, 2023

   Description:
   This program implements 'smallsh' - a small, custom shell in C. The smallsh system is 
   designed to interpret and execute a subset of features found in well-known shells like bash 
   while also handling both foreground and background processes, custom signal handling, 
   built-in commands, and support for IO redirection. Smallsh provides an alternative to standard
   shells in the UNIX API, and showcases the knowledge learned thus far in CS 374.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>

// Defining maximum input lengths for shell as described by assignment requirements
#define MAX_LENGTH 2048
#define MAX_ARGS 512

// Global variable to track the status of the last foreground process
int last_status = 0;

// Handler for SIGINT (Interrupt signal). 
// Ensures lets the foreground child process, if any, handle the SIGINT signal.
void handleSIGINT(int signo){
    // Intentionally left empty to allow foreground child processes to handle SIGINT
}

// Handler for SIGTSTP (Terminal Stop signal).
// This function toggles the state of the shell between normal mode and foreground-only mode.
void handleSIGTSTP(int signo){

    static int is_foreground_only = 0; // Static variable to keep track of the shell mode state
    is_foreground_only = !is_foreground_only; // Toggle the mode state: 0 (normal) to 1 (foreground-only)

    if (is_foreground_only){
        // When entering foreground-only mode, display a message.
        char* message = "\nEntering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, message, 50); // Used write for async-signal safety
    } else{
        // When exiting foreground-only mode, display a message.
        char* message = "\nExiting foreground-only mode\n";
        write(STDOUT_FILENO, message, 30); // Used write for async-signal safety
    }
}

// Function to implement execution of other commands using forks
void executeCommand(char** args, int is_background){
    pid_t pid; // Process ID for the forked process
    int status, in_redir = -1, out_redir = -1; // Variables for process status and I/O redirection

    pid = fork(); // Generate new process
    if (pid == 0){
        // Block is to be executed by the child process

        //detect background process - set permissions for foreground/background execution with signal processing
        if (is_background) {
            // Background processes must ignore SIGINT and SIGTSTP
            signal(SIGINT, SIG_IGN);
            signal(SIGTSTP, SIG_IGN);
        } else {
            // Foreground processes use default handling for SIGINT and ignore SIGTSTP
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_IGN);
        }

        // Handling input/output redirection
        for (int i = 0; args[i] != NULL; i++){

            // Input redirection
            if (strcmp(args[i], "<") == 0) {
                in_redir = open(args[i + 1], O_RDONLY); // Open the file for reading only. The file is specified by the argument after '<'

                // Check if the file opening was successful, output error if failure detected and exit process.
                if (in_redir == -1){
                    perror("smallsh");
                    exit(EXIT_FAILURE);
                }

                // Duplicate the file descriptor to standard input (STDIN_FILENO)
                dup2(in_redir, STDIN_FILENO);

                // Close the original file descriptor (no longer needed)
                close(in_redir);
                args[i] = NULL; // Nullify argument used for redirection to prevent it being executed as a command

            // Output redirection
            } else if (strcmp(args[i], ">") == 0) {
                out_redir = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644); // Open the file for writing. Create the file if it does not exist and truncate it if it does. set proper permissions for requirements. 

                // Check if the file opening was successful, output error if failure detected and exit process.
                if (out_redir == -1){
                    perror("smallsh");
                    exit(EXIT_FAILURE);
                }

                // Duplicate the file descriptor to standard output (STDOUT_FILENO)
                dup2(out_redir, STDOUT_FILENO);

                // Close the original file descriptor (no longer needed)
                close(out_redir);
                args[i] = NULL; // Nullify argument used for redirection to prevent it being executed as a command
            }
        }

        // Default I/O to /dev/null for background processes without specific redirection
        if (is_background){
            // Redirect input to /dev/null if no input redirection is specified
            // Important for background processes because if they only read from terminal, they could cause the shell to be unresponsive or have undefined behavior
            if (in_redir == -1){
                int devNullIn = open("/dev/null", O_RDONLY); // Open /dev/null for reading

                // Check if the file opening was successful, output error if failure detected and exit process
                if (devNullIn == -1){
                    perror("smallsh");
                    exit(EXIT_FAILURE);
                }

                // Duplicate the file descriptor to standard input (STDIN_FILENO)
                dup2(devNullIn, STDIN_FILENO);
                close(devNullIn);
            }
            // Redirect output to /dev/null if no output redirection is specified
            // Used to avoid cluttering the terminal with output from background processes.
            if (out_redir == -1){
                int devNullOut = open("/dev/null", O_WRONLY); // Open /dev/null for writing

                // Check if the file opening was successful, output error if failure detected and exit process
                if (devNullOut == -1){
                    perror("smallsh");
                    exit(EXIT_FAILURE);
                }

                // Duplicate the file descriptor to standard input (STDIN_FILENO)
                dup2(devNullOut, STDOUT_FILENO);
                close(devNullOut);
            }
        }

        // Execute the command
        if (execvp(args[0], args) == -1){ // If execvp returns, it means an error occurred as execvp only returns on failure. Print error message and exit
            perror("smallsh");
            exit(EXIT_FAILURE);
        }

    } else if (pid < 0){
        // Checks if fork() call was successful. If error forking detected, output error message.
        perror("smallsh");
    } else{
        // Parent process
        if (!is_background){
            // This loop uses waitpid() to wait for the child process pid to change state. Continues as long as the child process hasn't exited normally (WIFEXITED) or hasn't been terminated by a signal (WIFSIGNALED)
            do{
                waitpid(pid, &status, WUNTRACED); // WUNTRACED allows parent to return if a child has stopped
            } while (!WIFEXITED(status) && !WIFSIGNALED(status));
            // Update last_status variable with the exit status of the foreground process to give status command functionality and for termination signals.
            last_status = status;
        } else{
            // Handling background process. the parent process does not wait for it to complete
            printf("Background pid is %d\n", pid);
        }
    }
}

// Function to implement the 'exit' command
void exitShell(){
    exit(0);
}

// Function to implement the 'cd' command
void changeDirectory(char* path){
    if (path == NULL || strcmp(path, "~") == 0){
        // Change to the home directory if no path is specified or if path is "~"
        // Uses HOME environment variable to find the user's home directory
        chdir(getenv("HOME"));
    }else{
        // Change to the specified directory, if directory change fails print an error message
        if (chdir(path) != 0){
            perror("cd");
        }
    }
}

// Function to implement the 'status' command
void printStatus(){
    // Print exit status or the terminating signal of the last process
    if (WIFEXITED(last_status)){
        // If process exited normally, print exit status
        printf("exit value %d\n", WEXITSTATUS(last_status));
    } else{
        // If process terminated by signal, print signal number
        printf("terminated by signal %d\n", WTERMSIG(last_status));
    }
}

// Function to execute the built-in commands
int executeBuiltInCommand(char** args, int arg_count){
    // Check if the command is built-in command and executes it
    if (strcmp(args[0], "exit") == 0){
        exitShell();
        return 1; // Indicates a built-in command was executed
    }else if (strcmp(args[0], "cd") == 0){
        changeDirectory(args[1]); // Execute the cd command with the provided path spec
        return 1;
    }else if (strcmp(args[0], "status") == 0){
        printStatus();
        return 1;
    }
    return 0; // Return 0 if it is not a built-in command
}

// Function to parse the input line into arguments
void parseInput(char* input, char** args, int* arg_count){
    *arg_count = 0; // Initialize an argument count
    char* token = strtok(input, " "); // Tokenize the input string based on spaces

    while (token != NULL){
        args[(*arg_count)++] = token; // Add the token to the arguments array
        token = strtok(NULL, " "); // Continue through tokenizing the string
    }

    args[*arg_count] = NULL; // Null-terminate the arguments array for use in execvp
}

// Function to replace $$ with the shell's PID
void expandPID(char* input){
    char temp[MAX_LENGTH]; // Temporary string to hold the expanded input
    char* pid_placeholder = "$$"; // Placeholder string for PID
    char* found;

    // Get the current process ID and convert it to string
    pid_t pid = getpid();
    char pid_str[10];
    sprintf(pid_str, "%d", pid);

    // Replace all occurrences of $$ with shell's PID.
    while ((found = strstr(input, pid_placeholder)) != NULL){
        *found = '\0'; // Cut input at the location of $$

        strcpy(temp, input); // Copy the first part of input to temp
        strcat(temp, pid_str); // Append PID to temp
        strcat(temp, found + strlen(pid_placeholder)); // Append rest of the input
        strcpy(input, temp); // Copy expanded string back to original input
    }
}

int main(){
    // Signal handling setup for SIGINT and SIGTSTP
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

    // Setting SIGINT to be ignored by shell but caught by foreground processes
    SIGINT_action.sa_handler = handleSIGINT; // Determine the action commited on Ctrl-C press
    sigfillset(&SIGINT_action.sa_mask); // Blocks all signals while handling SIGINT to avoid interruptions
    SIGINT_action.sa_flags = SA_RESTART; // Ensures system calls interrupted by SIGINT are restarted
    sigaction(SIGINT, &SIGINT_action, NULL); // Applies the above settings

    // Setting up SIGTSTP to trigger a custom handler in shell
    SIGTSTP_action.sa_handler = handleSIGTSTP; // Determine the action commited on Ctrl-Z press
    sigfillset(&SIGTSTP_action.sa_mask); // Blocks all signals while handling SIGTSTP to prevent interruptions
    SIGTSTP_action.sa_flags = SA_RESTART; // Ensures system calls interrupted by SIGTSTP are restarted
    sigaction(SIGTSTP, &SIGTSTP_action, NULL); // Applies the above settings

    // Main program loop variables
    char input[MAX_LENGTH];  // Buffer for user inputs
    char *args[MAX_ARGS]; // Array to hold arguments
    int should_run = 1; // flag to determine when to exit program
    int arg_count; // To store the number of arguments

    // Main loop of the shell
    while (should_run){

        // Check and reap background processes to avoid zombie processes

        // Variables for storing the PID and status of finished background processes
        pid_t finished_pid;
        int status;

        // Loop to check and handle finished background processes using waitpid with WNOHANG to avoid blocking the shell
        while ((finished_pid = waitpid(-1, &status, WNOHANG)) > 0){
            printf("Background pid %d is done: ", finished_pid); // Report the PID of the completed background process

            // Report how the process terminated (normal exit or signal termination)
            if (WIFEXITED(status)){
                printf("exit value %d\n", WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)){
                printf("terminated by signal %d\n", WTERMSIG(status));
            }
        }

        printf(": "); // Displaying the command prompt symbol
        fflush(stdout); // Flushing output buffer to ensure prompt is printed

        // Reading the user input
        if (fgets(input, MAX_LENGTH, stdin) == NULL){
            fprintf(stderr, "Error reading input.\n");
            continue;
        }

        // Removing newline character at the end of input
        input[strcspn(input, "\n")] = 0;

        // Handling blank lines and comments
        if (strlen(input) == 0 || input[0] == '#'){
            continue; // Skipping through blank lines and comments
        }

        // Expanding any instances of $$ to shell's PID
        expandPID(input);

        // Parsing the input into arguments
        parseInput(input, args, &arg_count);

        // Verifying if the command should be run in the background
        int is_background = 0;
        if (arg_count > 0 && strcmp(args[arg_count - 1], "&") == 0){
            is_background = 1; // Set as a background command
            args[arg_count - 1] = NULL; // Remove '&' from arguments
            arg_count--; // Update argument count
        }
        
        // Execute built-in commands if present
        if (executeBuiltInCommand(args, arg_count)){
            continue; // Skip rest of loop for built-in commands
        }

        // Execute non-built-in commands after built in commands
        executeCommand(args, is_background);
    }

    return 0;
}