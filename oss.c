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
	int launched = 0;
	int activeProcesses = 0;
	int nextLaunchTime = 0;
	int linesWritten = 0;
	char *logFileName = "oss.log";

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

	FILE *file = fopen(logFileName, "w");
	if (!file) {
		printf("Error: failed opening log file. \n");
		exit(1);
	}

	// SIMULATED CLOCK
	int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), IPC_CREAT | 0666); // Creating shared memory using shmget.
	if (shmid == -1) { // If shmid is -1 as a result of shmget failing and returning -1, error message will print.
        	printf("Error: OSS shmget failed. \n");
        	exit(1);
    	}

	SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0); // Attach shared memory, clock is now a pointer to SimulatedClock structure.
	if (clock == (void *)-1) { // if shmat, the attaching shared memory function, fails, it returns an invalid memory address.
		printf("Error: OSS shared memory attachment failed. \n");
		exit(1);
	}

	// RESOURCE TABLE
	int shmResourceID = shmget(RESOURCE_KEY, sizeof(ResourceDesc) * NUM_RESOURCES, IPC_CREAT | 0666); // Creating shared memory using shmget.
	if (shmResourceID == -1) { // Error message in case creating shm fails.
    		printf("OSS Error: Failed to allocate shared memory for resource table");
    		exit(1);
	}
	
	ResourceDesc *resourceTable = (ResourceDesc *) shmat(shmResourceID, NULL, 0); // Attach shared memory, resourceTable is a pointer to ResourceDesc 
	if (resourceTable == (void *) -1) { // Error message in case of attatch fail.
    		printf("Error: OSS Failed to attach shared memory for resource table");
		exit(1);
	}
	
	// MESSAGE QUEUE
	int msgid = msgget(MSG_KEY, IPC_CREAT | 0666); // Setting up msg queue.
        if (msgid == -1) {
                printf("Error: OSS msgget failed. \n");
                exit(1);
        }

	// Initialize clock.
	clock->seconds = 0;
	clock->nanoseconds = 0;

	// Initialize PCB tables.
	for (int i = 0; i < MAX_PCB; i++) {
		processTable[i].occupied = 0;
    		processTable[i].pid = -1;
		processTable[i].startSeconds = 0;
	        processTable[i].startNano = 0;
		processTable[i].blocked = 0;

    		for (int j = 0; j < NUM_RESOURCES; j++) { // Initialize resource arrays to 0.
        		processTable[i].resourceAllocated[j] = 0;
        		processTable[i].maxResources[j] = 0;
		}
	}
	
	// Initialize Resource Table
	for (int i = 0; i < NUM_RESOURCES; i++) {
	     	resourceTable[i].totalInstances = INSTANCES_PER_RESOURCE;
	     	resourceTable[i].availableInstances = INSTANCES_PER_RESOURCE;
	    	resourceTable[i].head = 0;
	    	resourceTable[i].tail = 0;
	       
		for (int j = 0; j < MAX_PCB; j++) {
			resourceTable[i].resourceAllocated[j] = 0;
        		resourceTable[i].requestQueue[j] = -1; // -1 means empty slot in queue
	       	}
	}
	
	// Main loop
	while (launched < totalProcesses || activeProcesses > 0) {
		int randomNano = (rand() % 90001) + 10000;
		incrementClock(clock, 0, randomNano);

		int status; // For checking children that want to terminate.
		pid_t pid = waitpid(-1, &status, WNOHANG);

		if (pid > 0) { // Check if child terminated.
			for (int i = 0; i < MAX_PCB; i++) {
				if (processTable[i].occupied && processTable[i].pid == pid) { // Free PCB index if free. 
			                processTable[i].occupied = 0;
					activeProcesses--;
					if (linesWritten < 10000) { // Write to log file
						fprintf(file, "OSS: Child %d terminated at time %u:%u\n", pid, clock->seconds, clock->nanoseconds);
						linesWritten++;
						break;
					}
		    		}
			}
		}
		// Launching child 
		if (launched < totalProcesses && activeProcesses < simul && (clock->seconds * NANO_TO_SEC + clock->nanoseconds) >= nextLaunchTime) {
			int pcbIndex = -1; // Index for PCB table
			for (int i = 0; i < MAX_PCB; i++) { // Go through PCB table
				if (!processTable[i].occupied) { // Find free slot
					pcbIndex = i;
					break;
		    		}
			}

			if (pcbIndex != -1) { // For slot that is free
				pid_t childPid = fork(); // Split to user processes
				if (childPid == 0) { // Worker process
					char pcbIndexStr[10];
					snprintf(pcbIndexStr, sizeof(pcbIndexStr), "%d", pcbIndex); // Convert to string
					execl("./worker", "./worker", pcbIndexStr, NULL);
				} else { // Parent process
					// Update PCB table
					processTable[pcbIndex].occupied = 1;
                			processTable[pcbIndex].pid = childPid;
                			processTable[pcbIndex].startSeconds = clock->seconds;
                			processTable[pcbIndex].startNano = clock->nanoseconds;
              				
					// Update variables for next loop			
					activeProcesses++;
                			launched++;
                			
					nextLaunchTime = clock->seconds * NANO_TO_SEC + clock->nanoseconds + (interval * 1000000); // Set up next user process launch
                			if (linesWritten < 10000) {
						fprintf(file, "OSS: Forked child %d at time %u:%u\n", childPid, clock->seconds, clock->nanoseconds);
						linesWritten++;
					}
				}
			}
		}

		OssMSG msg;
		while (msgrcv(msgid, &msg, sizeof(OssMSG) - sizeof(long), 0, IPC_NOWAIT) > 0) {
			if (msg.quantity > 0) {
				if (linesWritten < 10000) {
					fprintf(file, "OSS: Process %d requesting R%d x%d at time %u:%u\n", msg.pid, msg.resourceID, msg.quantity, clock->seconds, clock->nanoseconds);
			       		linesWritten++;
				}
			} else {
				if (linesWritten < 10000) {
					fprintf(file, "OSS: Process %d releasing R%d at time %u:%u\n", msg.pid, msg.resourceID, clock->seconds, clock->nanoseconds);
		    			linesWritten++;
				}
			}
		}
		if (linesWritten >= 10000) {
			fprintf(file, "OSS: Log limit of 10,000 lines reached.\n");
			break;
	    	}
	}

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
