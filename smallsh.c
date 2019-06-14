// Michael Childress
// smallsh

/*
 * NAME
 *   smallsh - a minimally functional UNIX Shell
 * SYNOPSIS
 *   smallsh
 * DESCRIPTION
 *   smallsh continues to prompt the user for input until they type exit. It also has cd and status command functionality
 *   built into the program. For all other shell commands, execvp() is used to invoke the pre-built functions. The program
 *   can expand any instance of $$ to the PID of the shell process. It can run commands in the background if & is the final
 *   argument sent at the command line. This background functionality can be turned on/off by sending a SIGTSTP signal
 *   (pressing Ctrl-Z). It can also terminate foreground children when a SIGINT signal is sent, but keep background children
 *   and itself active.
 * AUTHOR
 *   Written by Michael Childress
*/



#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>


// Global variables were needed so the SIGTSTP signal handler could properly check for exit status of the previous foreground command

bool turnOffBackground = false;		// Global variable will be used by SIGTSTP handler to turn background mode on/off
pid_t currentForegroundPID;		// Used by SIGTSTP handler to make sure shell waits for currently running foreground process to finish
int foreChildExitMethod = -5;


// SIGTSTP signal handler

/*
 * NAME
 *   catchSIGTSTP - a signal handler for SIGTSTP
 * SYNOPSIS
 *   catchSIGTSTP(int signo) assigned to SIGTSTP via sigaction()
 * DESCRIPTION
 *   A signal handler used when the parent process receives a SIGTSTP signal. It controls whether background commands are acknowledged.
 *   The first time it is called, background command functionality is turned off and everything is treated as a foreground command.
 *   When another SIGTSTP is sent, background command functionality is turned on again. The appropriate text messages are displayed to
 *   the user as well.
 * AUTHOR
 *   Written by Michael Childress
*/


void catchSIGTSTP(int signo) {

	waitpid(currentForegroundPID, &foreChildExitMethod, 0);

	if(turnOffBackground) {		// If background is currently turned off, turn it on again

		turnOffBackground = false;
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
	}

	else {		// Background is currently on, so turn it off

		turnOffBackground = true;
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
	}

}



/*
 * NAME
 *   tryToRunCommand - attempts to run user entered command using execvp()
 * SYNOPSIS
 *   tryToRunCommand(char** argumentArray, bool isBackground)
 * DESCRIPTION
 *   This function is called by the child process immediately after it's created to try and run execvp() on the user entered command.
 *   If the command is a background one, the trailing & is removed. The function then checks for any necessary I/O redirection, and
 *   makes these changes accordingly. execvp() is called and if this fails, the function prints an error message and returns -1.
 * AUTHOR
 *   Written by Michael Childress
*/

// This gets called by the child process immediately after it's created to try and run exec()

int tryToRunCommand(char** argumentArray, bool isBackground) {


	int execvpStatus;
	int lastIndex;

	int inputFD;
	int outputFD;
	int dup2Result;

	bool inputRedirected = false;		// These bools make sure that background processes get sent to /dev/null if not redirected
	bool outputRedirected = false;

	int devNullIn;		// File descriptor for /dev/null if we have to open that for a background process
	int devNullOut;

	// Check for any I/O redirection in the argument array
	
	// Find last array position
	
	for(int index = 0; index < 512; index++) {

		if(argumentArray[index] != NULL) {
			lastIndex = index;
		}

	}


	if(isBackground) {	// Remove the & and send lastIndex back one position because & no longer present

		argumentArray[lastIndex] = NULL;
		lastIndex = lastIndex - 1;
	}

	// Check for redirection in the last two arguments of the array
	
	if(lastIndex >= 1) {

		if(strcmp(argumentArray[lastIndex - 1], "<\0") == 0) {		// Last arguments are input redirection

			inputFD = open(argumentArray[lastIndex], O_RDONLY);
			if(inputFD == -1) {printf("cannot open %s for input\n", argumentArray[lastIndex]); fflush(stdout); return -1;}
			dup2(inputFD, 0);					// Redirect stdin to this file to read from
			argumentArray[lastIndex] = NULL;
			argumentArray[lastIndex - 1] = NULL;			// < and fileName are not included in execvp call
			
			inputRedirected = true;
		}


		else if(strcmp(argumentArray[lastIndex - 1], ">\0") == 0) {	// Last arguments are output redirection

			outputFD = open(argumentArray[lastIndex], O_WRONLY | O_CREAT | O_TRUNC, 0777);
			if(outputFD == -1) {printf("cannot open %s for output\n", argumentArray[lastIndex]); fflush(stdout); return -1;}
			dup2(outputFD, 1);					// Redirect stdout to this file to write to
			argumentArray[lastIndex] = NULL;
			argumentArray[lastIndex - 1] = NULL;

			outputRedirected = true;
		}

	}

	if(lastIndex >= 3) {

		if(strcmp(argumentArray[lastIndex - 3], "<\0") == 0) {		// Second to last arguments are input redirection

			inputFD = open(argumentArray[lastIndex - 2], O_RDONLY);
			if(inputFD == -1) {printf("cannot open %s for input\n", argumentArray[lastIndex - 2]); fflush(stdout); return -1;}
			dup2(inputFD, 0);
			argumentArray[lastIndex - 2] = NULL;
			argumentArray[lastIndex - 3] = NULL;

			inputRedirected = true;
		}

	
		else if(strcmp(argumentArray[lastIndex - 3], ">\0") == 0) {	// Second to last arguments are output redirection

			outputFD = open(argumentArray[lastIndex - 2], O_WRONLY | O_CREAT | O_TRUNC, 0777);
			if(outputFD == -1) {printf("cannot open %s for output\n", argumentArray[lastIndex - 2]); fflush(stdout); return -1;}
			dup2(outputFD, 1);
			argumentArray[lastIndex - 2] = NULL;
			argumentArray[lastIndex - 3] = NULL;

			outputRedirected = true;
		}

	}

	
	// Make sure background process does not point to terminal if not redirected by user
	
	if(isBackground && !inputRedirected) {		// Get stdin from /dev/null so command isn't waiting on terminal input

		devNullIn = open("/dev/null", O_RDONLY);
		if(devNullIn == -1) {printf("cannot open /dev/null for input\n"); fflush(stdout); return -1;}
		dup2(devNullIn, 0);

	}

	if(isBackground && !outputRedirected) {		// Send stdout to /dev/null so it doesn't appear on the terminal

		devNullOut = open("/dev/null", O_WRONLY);
		if(devNullOut == -1) {printf("cannot open /dev/null for output\n"); fflush(stdout); return -1;}
		dup2(devNullOut, 1);
	}


	execvpStatus = execvp(argumentArray[0], argumentArray);	

	// If this line gets reached, then the exec process failed
	
	printf("%s: no such file or directory\n", argumentArray[0]);
	fflush(stdout);
	return -1;

}



/*
 * NAME
 *   dollarsToPID - convert an instance of $$ in a string into the process id of the shell
 * SYNOPSIS
 *   dollarsToPID(char* inputString)
 * DESCRIPTION
 *   This function is called after every user input to make sure any $$ gets changed to the process ID of the shell before the string is
 *   tokenized.
 * AUTHOR
 *   Written by Michael Childress
*/

// Scans the user input and converts any $$ to the PID of the shell process

char* dollarsToPID(char* inputString) {

	char correctedString[2048];
	memset(correctedString, '\0', sizeof(correctedString));

	char PIDAsString[50];

	int processID = getpid();

	sprintf(PIDAsString, "%d", processID);

	char* token;

	bool stringNeededCorrecting = false;



	for(int position = 0; position < strlen(inputString); position++) {

		if(inputString[position] == '$' && position+1 < strlen(inputString) && inputString[position+1] == '$') {

			token = strtok(inputString, "$");

			strcat(correctedString, token);		// Everything up to the $$

			strcat(correctedString, PIDAsString);

			token = strtok(NULL, "$");		// Remove second dollar sign

		
			if(token != NULL) {
				strcat(correctedString, token); // Rest of string
			}


			stringNeededCorrecting = true;		// Make sure to return the corrected string
		}

	}

	
	if(stringNeededCorrecting) {

		inputString = correctedString;
		return inputString;
	}

	else {
		return inputString;
	}
}



/*
 * NAME
 *   main - the main function of the smallsh program
 * SYNOPSIS
 *   Called immediately when program begins
 * DESCRIPTION
 *   Asks for user input in a do while loop that continues until user types exit on the command line. It begins by checking for any
 *   background processes that are ready to be terminated. After user types their input into the command prompt, the string is sent to
 *   dollarsToPID() to change any $$ to the process ID of the shell. The input is then tokenized and put in an argument array.
 *   It checks to see if user asked for a built in command, entered a blank line, or a comment line (beginning with #), and if none of
 *   those are triggered, a child process is forked in either foreground or background mode(if & is last argument), and the child sends 
 *   the argument array to the tryToRunCommand() function to run execvp(). Child processes are cleaned up by the parent when they are 
 *   finished. It then checks to see if the process exited or was killed by a signal and stores this information for later retrieval by 
 *   the status command. Before the program terminates, any remaining children are cleaned up.
 * AUTHOR
 *   Written by Michael Childress
*/

int main() {



	int inputPositionToFill;
	char* argumentArray[512];		// Will be able to support 1 command + 512 arguments
	bool userTypedExit = false;

	int foreGroundProcessResult;		// If value stays 0, then the foreground process worked. If it becomes -1, then it didn't

	char* userInput = NULL;		// User input allows for total command length of 2048 characters
	char* userInputFixed = NULL;
	size_t bufferSize = 2048;

	int exitStatusCode;	// Will hold exit status code of terminated foreground child
	bool noForegroundProcessesRun = true;	// Will get set to false once one foreground process is run. controls the activity of status command
	int termSignal;		// Will hold signal number that terminated foreground child

	char* token;

	char* filePathToken;

	char* HOME = "HOME";	// To change the current working directory to HOME

	char filePath[2048];	// This will be used to check if the first character entered in cd command is a / which would mean absolute filepath
	char pathToOpen[2048];	// Will contain the complete file path to send to chdir()

	pid_t childPid = -5;		// Process ID of any spawned child process
	int childExitMethod = -5;	// Will contain details about what terminated the child

	memset(filePath, '\0', sizeof(filePath));	// make sure we have a clean string
	memset(pathToOpen, '\0', sizeof(pathToOpen));

	char firstArgument[2048];	// This will be used to scan the first user argument's first character to make sure it isn't #
	memset(firstArgument, '\0', sizeof(firstArgument));

	int lastIndex;		// Used to check for the presence of & in entered command


	int backgroundChildExitMethod = -5;
	pid_t backgroundPIDs[200];	// This will store PIDs of background processes that are waiting to finish

	for(int nullIndex = 0; nullIndex < 200; nullIndex++) {

		backgroundPIDs[nullIndex] = -5;		// Stuff a bogus value in each position of the PID array
	}

	int nextOpenPosition = 0;	// Will increment as we add processes to the array. Once 199 is reached it will loop back to 0
	pid_t backgroundChildPID = -5;

	pid_t dummyPID = -5;	// Used to check if a background process has actually completed after each loop

	int backgroundExitStatusCode;
	int backgroundTermSignal;

	int numCharsEntered = -5;	// Tracks how many characters getline receives (prevents issues with signal interruption)


	// Prepare the parent process to ignore SIGINT
	
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &SIGINT_action, NULL);

	bool killedByExit = false;		// These bools are used by the signal command to know whether to print the exit status or signal number
	bool killedBySignal = false;


	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);


	do {

		for(int index=0; index < 512; index++) {

			argumentArray[index] = NULL;		// Reset the argument array before each command is entered

		}


		// Check for any background children ready to be cleaned up
		for(int reaperIndex = 0; reaperIndex < 200; reaperIndex++) {

			if(backgroundPIDs[reaperIndex] != -5) {
				dummyPID = waitpid(backgroundPIDs[reaperIndex], &backgroundChildExitMethod, WNOHANG);

				if(dummyPID != 0) {		

					if(WIFEXITED(backgroundChildExitMethod) != 0) {

						backgroundExitStatusCode = WEXITSTATUS(backgroundChildExitMethod);
						printf("background pid %d is done: exit value %d\n", (int)backgroundPIDs[reaperIndex], backgroundExitStatusCode);
						fflush(stdout);
					
						backgroundPIDs[reaperIndex] = -5;	// No longer an active PID
					}

					if(WIFSIGNALED(backgroundChildExitMethod) != 0) {
						backgroundTermSignal = WTERMSIG(backgroundChildExitMethod);
						printf("background pid %d is done: terminated by signal %d\n", (int)backgroundPIDs[reaperIndex], backgroundTermSignal);
						fflush(stdout);
						backgroundPIDs[reaperIndex] = -5;
					}

					
			
					backgroundChildExitMethod = 0;
				}
			}
		}

		
		while(1) {

			printf(": ");		// Display the prompt to the user
			fflush(stdout);		// Flush the buffer to ensure text is printed

			userInput = NULL;
			bufferSize = 0;

			numCharsEntered = getline(&userInput, &bufferSize, stdin);	// Get the input from the user

			if(numCharsEntered == -1) {	// If signal interrupted, get ready to ask for input again
				clearerr(stdin);
			}

			else {
				break;
			}
			
		}

		token = strtok(userInput, "\n");
		userInputFixed = token;	

		if(userInputFixed == NULL) {			// User typed blank line just start a new loop
			memset(firstArgument, '\0', sizeof(firstArgument));
			free(userInput);	// Free dynamic memory used by getline
			continue;
		}

		userInputFixed = dollarsToPID(userInputFixed);	// Check for and expand any $$ to the shell PID
		

		token = strtok(userInputFixed, " ");		// Grab the first word entered on the command line


		argumentArray[0] = token;


		inputPositionToFill = 1;	// Index in the argument array starts filling at 1 because we've already read in position 0
	
		while(token != NULL) {		// Read in remaining words

			token = strtok(NULL, " ");
			if(inputPositionToFill <= 511) {	// User can enter at most 512 arguments

				argumentArray[inputPositionToFill] = token;

				inputPositionToFill++;
			}
		}



		strcpy(firstArgument, argumentArray[0]);	// Later used to check for # at the beginning of the line


		if(strcmp(argumentArray[0], "exit\0") == 0) {	// User typed in exit as their chosen command


			userTypedExit = true;		// End the while loop
		}

		else if(strcmp(argumentArray[0], "cd\0") == 0 && argumentArray[1] == NULL) {	// User typed cd with no argument

			chdir(getenv(HOME));	// Change directory to the HOME directory	

		}


		else if(strcmp(argumentArray[0], "cd\0") == 0 && argumentArray[1] != NULL) {	// User typed cd with one argument


			strcpy(filePath, argumentArray[1]);

			if(filePath[0] == '/') {	// User is supplying an absolute path

				
				strcat(pathToOpen, getenv(HOME));			// new filepath = HOME + user input
				strcat(pathToOpen, filePath);

				filePathToken = strtok(pathToOpen, "\n");	// Need to remove the \n at the end of the string

				chdir(filePathToken);

			}


			else {		// User supplied a relative path

				getcwd(pathToOpen, (size_t)sizeof(pathToOpen));		// new filepath = cwd/userInput
				strcat(pathToOpen, "/");
				strcat(pathToOpen, filePath);

				filePathToken = strtok(pathToOpen, "\n");	// Need to remove the \n at the end of the string

				chdir(filePathToken);				

			}

			memset(filePath, '\0', sizeof(filePath));
			memset(pathToOpen, '\0', sizeof(pathToOpen));

		}

		

		else if(strcmp(argumentArray[0], "#\0") == 0 || strcmp(argumentArray[0], "#\n") == 0 || firstArgument[0] == '#') {	// Comment line
			// Do nothing
		}



		else if(strcmp(argumentArray[0], "status\0") == 0) {		// User wants the status command

			if(noForegroundProcessesRun) {		// If run before a foreground process is run, just return exit status 0

				printf("exit value 0\n");
				fflush(stdout);
			}

			else {
				if(killedByExit) {				// We've had a foreground process complete at least once

					printf("exit value %d\n", exitStatusCode);
					fflush(stdout);
				}

				else if(killedBySignal) {
					printf("terminated by signal %d\n", termSignal);
					fflush(stdout);
				}
			}
		}

		else {		// We need to create a child process

			// Find last array position
	
			for(int index = 0; index < 512; index++) {

				if(argumentArray[index] != NULL) {
					lastIndex = index;
				}

			}


			if(turnOffBackground) {		// Find and remove the & if background commands are currently not allowed

				if(strcmp(argumentArray[lastIndex], "&\0") == 0) {
					argumentArray[lastIndex] = NULL;
					lastIndex = lastIndex - 1;
				}			

			}

			
			if(strcmp(argumentArray[lastIndex], "&\0") == 0) {	// Run the command in background mode

				backgroundChildPID = fork();	// Will NOT wait for child immediately
				
				if(backgroundChildPID == -1) {
					perror("Major problem creating background child!\n");
					exit(1);
				}

				else if(backgroundChildPID == 0) {	// We are in the child process

					SIGTSTP_action.sa_handler = SIG_IGN;	// Background children should ignore SIGTSTP
					sigaction(SIGTSTP, &SIGTSTP_action, NULL);
					tryToRunCommand(argumentArray, true);		// Will try to call execvp. True means it's background
					exit(1);	// Only reached if there was a problem
				}

				else {		// We are in the parent process
					printf("background pid is %d\n", backgroundChildPID);
					fflush(stdout);
					backgroundPIDs[nextOpenPosition] = backgroundChildPID;		// Store the child PID for later use
	

					if(nextOpenPosition == 199) {
						nextOpenPosition = 0;
					}

					else {
						nextOpenPosition++;

					}
				}

			}


			else {					// Run the command in foreground mode

				childPid = fork();

				if(childPid == -1) {
					perror("Major problem creating child!\n");
					exit(1);
				}	


				else if(childPid == 0) {	// We are in the child process
	
					SIGINT_action.sa_handler = SIG_DFL;	// Foreground process must be terminated by SIGINT
					sigaction(SIGINT, &SIGINT_action, NULL);

					SIGTSTP_action.sa_handler = SIG_IGN;	// Foreground children should ignore SIGTSTP
					sigaction(SIGTSTP, &SIGTSTP_action, NULL);


					tryToRunCommand(argumentArray, false);		// Will try to call execvp. False means it's foreground
					exit(1);	// Only reached if there was a problem
				}

				else {		// We are in the parent process

					currentForegroundPID = childPid;	// In case SIGTSTP handler need to wait for it
					waitpid(childPid, &foreChildExitMethod, 0);		// Parent waits here until child is done
					noForegroundProcessesRun = false;		

	
					if(WIFSIGNALED(foreChildExitMethod) != 0) {		// A signal killed the child
						termSignal = WTERMSIG(foreChildExitMethod);
						printf("terminated by signal %d\n", termSignal);
						fflush(stdout);
						killedBySignal = true;
						killedByExit = false;
					}
	
					if(WIFEXITED(foreChildExitMethod) != 0) {		// Child exited normally
						exitStatusCode = WEXITSTATUS(foreChildExitMethod);
						killedBySignal = false;
						killedByExit = true;

					}

				}

			}

		}

		

		// Make sure to free dynamic memory used by user input each time this point reached

		memset(firstArgument, '\0', sizeof(firstArgument));

		free(userInput);	// Free dynamic memory used by getline
	
	}while(userTypedExit == false);		// Once user types exit our shell should quit

	



	// Need to terminate all active child processes
	
	for(int reaperIndex = 0; reaperIndex < 200; reaperIndex++) {

		if(backgroundPIDs[reaperIndex] != -5) {

			kill(backgroundPIDs[reaperIndex], SIGKILL);		// Kill all background processes before exiting

		}

	}


	return 0;
}
