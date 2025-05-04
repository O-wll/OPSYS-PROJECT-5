#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h> // For shared memory
#include <sys/ipc.h> // Also for shared memory, allows worker class to access shared memory
#include <time.h>
#include <string.h> // For memset
#include "oss.h"

#define NANO_TO_SEC 1000000000

void incrementClock(SimulatedClock *clock, int addSec, int addNano); // Clock increment
void signalHandler(int sig);
void help();

PCB processTable[MAX_PCB]; // Process Table 

int main(int argc, char **argv) {
	int totalProcesses = 40;
	int simul = 18;
	int interval = 500;
	int userInput = 0;
	char *logFileName = NULL;
	FILE *logFilePtr = NULL;

	// User Input handler
	while ((userInput = getopt(argc, argv, "n:s:i:f:h")) != -1) {
		switch(userInput) {
			case 'n': // How many child processes to launch.
				totalProcesses = atoi(optarg);
				if (totalProcesses <= 0) {
					printf("Error: Total child processes must be at least one. \n");
					exit(1);
				}
				break;
			case 's': // How many simulations to run at once.
				simul = atoi(optarg);
				// Ensuring simulations isn't zero or below for the program to work.
				if (simul < 0) {
					printf("Error: Simulations must be positive. \n");
					exit(1);
				}
				break;
			case 'i': // How often to launch child interval
				interval = atoi(optarg);
				if (interval <= 0) {
					printf("Error: interval must be positive. \n");
                                        exit(1);
                                }
				break;
			case 'f': // Input name of log file
				logFileName = optarg;
                                break;
			case 'h': // Prints out help function.
				help();
				return 0;
			case '?': // Invalid user argument handling.
				printf("Error: Invalid argument detected \n");
				printf("Usage: ./oss.c -h to learn how to use this program \n");
				exit(1);
		}
	}
	
	// Start Alarm
	alarm(60);
	signal(SIGINT, signalHandler);
	signal(SIGALRM, signalHandler);
	
	return 0;
}

void incrementClock(SimulatedClock *clock, int addSec, int addNano) { // This function simulates the increment of our simulated clock.
	clock->seconds += addSec;
	clock->nanoseconds += addNano;

	while (clock->nanoseconds >= NANO_TO_SEC) {
		clock->seconds++;
        	clock->nanoseconds -= NANO_TO_SEC;
    }
}


void signalHandler(int sig) { // Signal handler
       	// Catching signal
	if (sig == SIGALRM) { // 60 seconds have passed
	       	fprintf(stderr, "Alarm signal caught, terminating all processes.\n");
       	}
     	else if (sig == SIGINT) { // Ctrl-C caught
	       	fprintf(stderr, "Ctrl-C signal caught, terminating all processes.\n");
       	}

	for (int i = 0; i < MAX_PCB; i++) { // Kill all processes.
		if (processTable[i].occupied) {
			kill(processTable[i].pid, SIGTERM);
	    	}
	}

	// Cleanup shared memory
    	int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);
    	if (shmid != -1) {
		SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0);
		if (clock != (void *)-1) { // Detach shared memory
			if (shmdt(clock) == -1) {
				printf("Error: OSS Shared memory detachment failed \n");
				exit(1);
			}
		}
		// Remove shared memory
		if (shmctl(shmid, IPC_RMID, NULL) == -1) {
                	printf("Error: Removing memory failed \n");
                	exit(1);
        	}
	}

	// Cleanup message queue
    	int msgid = msgget(MSG_KEY, 0666);
    	if (msgid != -1) {
		if (msgctl(msgid, IPC_RMID, NULL) == -1) {
		    	printf("Error: Removing msg queue failed. \n");
		       	exit(1);
	       	}
       	}

	exit(1);
}

void help() {
	printf("Hi");
}
