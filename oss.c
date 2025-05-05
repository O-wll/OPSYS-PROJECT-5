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
	int grantsCount = 0;
	int verbose = 0;
	unsigned int lastPrintSec = 0;
	unsigned int lastPrintNano = 0;
	int totalRequests = 0;
	int grantedInstantly = 0;
	int grantedAfterWait = 0;
	int deadlockDetectedRun = 0;
	int deadlockTerminations = 0;
	int deadlockProcesses = 0;
	int terminations = 0;
	time_t startTime = time(NULL);

	// User Input handler
	while ((userInput = getopt(argc, argv, "n:s:i:f:hv")) != -1) {
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

				if (simul > 18) {
					printf("Simulations CANNOT exceed 18 \n");
					simul = 18;
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
			case 'v':
				verbose = 1;
				break;
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
		
		if (difftime(time(NULL), startTime) >= 5) { // Track if 5 seconds in REAL TIME has passed.
		    	printf("OSS: Real-time limit of 5 seconds reached. Terminating simulation.\n");
		    	if (linesWritten < 10000 && verbose) {
				fprintf(file, "OSS: Real-time limit of 5 seconds reached. Terminating simulation.\n");
				linesWritten++;
		    	}
		    	break;
		}

		int status; // For checking children that want to terminate.
		pid_t pid = waitpid(-1, &status, WNOHANG);

		static unsigned int lastDeadlockCheck = 0;

		if (clock->seconds > lastDeadlockCheck) { // Dead lock detection.
		    	lastDeadlockCheck = clock->seconds; // Last second stored

			deadlockDetectedRun++;

			int deadlockFound = 0; // flag if deadlock detected
		    	int victim = -1; // process that was deadlocked

		    	for (int i = 0; i < MAX_PCB; i++) { // Search every process that is active and blocked.
				if (processTable[i].occupied && processTable[i].blocked) {
			    		int canBeGranted = 0;
			    		for (int j = 0; j < NUM_RESOURCES; j++) { // Check if process can be granted resources
						int need = processTable[i].maxResources[j] - processTable[i].resourceAllocated[j];
						if (need > 0 && resourceTable[j].availableInstances >= need) {
				    			canBeGranted = 1;
				    			break;
						}
			    		}
			    		if (!canBeGranted) { // If resource cannot be allocated, mark it as deadlocked.
						deadlockFound = 1;
						victim = i;
						deadlockProcesses++;
						break; // just resolve one per second
			    		}
				}
		    	}

			if (deadlockFound && victim != -1) { // Dealing with deadlocked processes.
				if (linesWritten < 10000) {
					fprintf(file, "OSS: Deadlock detected at time %u:%u. Terminating P%d\n", clock->seconds, clock->nanoseconds, processTable[victim].pid);
					printf("OSS: Deadlock detected at time %u:%u. Terminating P%d\n", clock->seconds, clock->nanoseconds, processTable[victim].pid);
					linesWritten++;
				}
				for (int j = 0; j < NUM_RESOURCES; j++) {
			    		int released = processTable[victim].resourceAllocated[j];
			    		if (released > 0) { // Whatever resources that is held by deadlocked process, release.
						resourceTable[j].availableInstances += released;
						resourceTable[j].resourceAllocated[victim] = 0;
						processTable[victim].resourceAllocated[j] = 0;
			    		}
				}

				// Terminate process and reset its PCB.
				kill(processTable[victim].pid, SIGTERM);
				processTable[victim].occupied = 0;
				processTable[victim].pid = -1;
				processTable[victim].blocked = 0;
				deadlockTerminations++;
		    	}
		}

		if (pid > 0) { // Check if child terminated.
			for (int i = 0; i < MAX_PCB; i++) {
				if (processTable[i].occupied && processTable[i].pid == pid) { // Free PCB index if free. 
			                processTable[i].occupied = 0;
					activeProcesses--;
					terminations++;
					if (linesWritten < 10000 && verbose) { // Write to log file
						fprintf(file, "OSS: Child %d terminated at time %u:%u\n", pid, clock->seconds, clock->nanoseconds);						                                             printf("OSS: Child %d terminated at time %u:%u\n", pid, clock->seconds, clock->nanoseconds);
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
					execl("./worker", "./worker", NULL);
				} else { // Parent process
					// Update PCB table
					processTable[pcbIndex].occupied = 1;
                			processTable[pcbIndex].pid = childPid;
                			processTable[pcbIndex].startSeconds = clock->seconds;
                			processTable[pcbIndex].startNano = clock->nanoseconds;
					
					// Max resource claim
					for (int j = 0; j < NUM_RESOURCES; j++) {
					    	processTable[pcbIndex].maxResources[j] = rand() % (INSTANCES_PER_RESOURCE + 1);
					}					

					// Update variables for next loop			
					activeProcesses++;
                			launched++;
                			
					nextLaunchTime = clock->seconds * NANO_TO_SEC + clock->nanoseconds + (interval * 1000000); // Set up next user process launch
                			if (linesWritten < 10000 && verbose) {
						fprintf(file, "OSS: Forked child %d at time %u:%u\n", childPid, clock->seconds, clock->nanoseconds);
						printf("OSS: Forked child %d at time %u:%u\n", childPid, clock->seconds, clock->nanoseconds);
						linesWritten++;
					}
				}
			}
		}

		OssMSG msg;
		while (msgrcv(msgid, &msg, sizeof(OssMSG) - sizeof(long), 0, IPC_NOWAIT) > 0) { // Get message from message queue
			// Find an active PCB process
			 int pcbIndex = -1;
			 for (int i = 0; i < MAX_PCB; i++) {
			 	 if (processTable[i].occupied && processTable[i].pid == msg.pid) {
			     		 pcbIndex = i;
			     		 break;
			 	 }
		     	 }

			 if (pcbIndex == -1) { // If no pcb processes are found
			 	continue;
			 }

			 int resourceID = msg.resourceID; // Get resource ID from worker.

			if (msg.quantity > 0) { // Request resources
				totalRequests++; // Update requests amount

				if (resourceTable[resourceID].availableInstances >= msg.quantity) { // Check if available instances for resource.
					grantedInstantly++; // Update granted request instantly

					resourceTable[resourceID].availableInstances -= msg.quantity; // Granting resource request meaning reducing how much is available once granted.
			    		resourceTable[resourceID].resourceAllocated[pcbIndex] += msg.quantity; // Update that pcbIndex is holding this resource
			    		processTable[pcbIndex].resourceAllocated[resourceID] += msg.quantity; // Update PCB table for resource allocated

					// Send message to worker
					OssMSG response;
			    		// Send message that tells worker that message was granted.		
					response.mtype = msg.pid;
					response.pid = msg.pid;
			    		response.resourceID = resourceID;
			    		response.quantity = msg.quantity; // Positive means granted
			    		msgsnd(msgid, &response, sizeof(OssMSG) - sizeof(long), 0);

					grantsCount++; // Update requests granted count

					if (grantsCount >= 20 && linesWritten < 10000) { // Print resource allocation to PCB every 20 granted requests.
					    	fprintf(file, "OSS: Allocation Table after 20 Grants at %u:%u\n", clock->seconds, clock->nanoseconds);
						printf("OSS: Allocation Table after 20 Grants at %u:%u\n", clock->seconds, clock->nanoseconds);
						linesWritten++;
					    	
						for (int i = 0; i < MAX_PCB; i++) { // Printing PCB index
							if (processTable[i].occupied) {
						    		fprintf(file, "P%d: ", processTable[i].pid);
						    		for (int j = 0; j < NUM_RESOURCES; j++) { // Printing resources allocated
									fprintf(file, "R%d=%d ", j, processTable[i].resourceAllocated[j]);
						    		}
						    		fprintf(file, "\n");
						    		linesWritten++;
							}    
						}
					    	grantsCount = 0; // Reset counter
					}

					if (linesWritten < 10000 && verbose) {
						fprintf(file, "OSS: Process %d requesting R%d x%d at time %u:%u\n", msg.pid, msg.resourceID, msg.quantity, clock->seconds, clock->nanoseconds);
                                                printf("OSS: Process %d requesting R%d x%d at time %u:%u\n", msg.pid, msg.resourceID, msg.quantity, clock->seconds, clock->nanoseconds);
						linesWritten++;
					}
				} else { // In case there's not enough resources to allocate.
					
					// Add process to wait queue and block it until resources are allocated.
					int tail = resourceTable[resourceID].tail;
			    		resourceTable[resourceID].requestQueue[tail] = pcbIndex;
			    		resourceTable[resourceID].tail = (tail + 1) % MAX_PCB;
			    		processTable[pcbIndex].blocked = 1;

					if (linesWritten < 10000 && verbose) {
						fprintf(file, "OSS: P%d blocked for R%d at %u:%u\n", msg.pid, resourceID, clock->seconds, clock->nanoseconds);						                                     printf("OSS: P%d blocked for R%d at %u:%u\n", msg.pid, resourceID, clock->seconds, clock->nanoseconds);
						linesWritten++;
			    		}
				}
			} else { // Releasing Resources
				resourceID = msg.resourceID;
				pcbIndex = -1;

				// Find active PCB process 
				for (int i = 0; i < MAX_PCB; i++) {
					if (processTable[i].occupied && processTable[i].pid == msg.pid) {
				    		pcbIndex = i;
				    		break;
					}
				}

				if (pcbIndex == -1) { // If not, skip and continue while loop.
					continue;
				}
				
				// How much resources is process releasing
				int amountReleased = processTable[pcbIndex].resourceAllocated[resourceID];

				// Releasing resources.
				resourceTable[resourceID].availableInstances += amountReleased;
			    	resourceTable[resourceID].resourceAllocated[pcbIndex] = 0;
			    	processTable[pcbIndex].resourceAllocated[resourceID] = 0;

				if (linesWritten < 10000 && verbose) {
					fprintf(file, "OSS: Process %d releasing R%d at time %u:%u\n", msg.pid, msg.resourceID, clock->seconds, clock->nanoseconds);
					printf("OSS: Process %d releasing R%d at time %u:%u\n", msg.pid, msg.resourceID, clock->seconds, clock->nanoseconds);
					linesWritten++;
				}

				// For process that are blocked that need the resource. 
				
				int head = resourceTable[resourceID].head;
				int tail = resourceTable[resourceID].tail;
				
				while (head != tail) { // Run through every blocked process

					int blockedIndex = resourceTable[resourceID].requestQueue[head];
					
					if (blockedIndex == -1) { // Check if resourceQueue is empty, if so, continue.
						head = (head + 1) % MAX_PCB;
						continue;
					}
					
					// Determine how much resource the blocked process wants. 
					int maxAllowed = processTable[blockedIndex].maxResources[resourceID];
					int alreadyHeld = processTable[blockedIndex].resourceAllocated[resourceID];
					int remainingRequest = maxAllowed - alreadyHeld;

					if (resourceTable[resourceID].availableInstances >= remainingRequest && remainingRequest > 0) { // Grant request
						// Allocating resources to process 
						resourceTable[resourceID].availableInstances -= remainingRequest;
						resourceTable[resourceID].resourceAllocated[blockedIndex] += remainingRequest;
						processTable[blockedIndex].resourceAllocated[resourceID] += remainingRequest;
						processTable[blockedIndex].blocked = 0;

						// Send message indicating request granted
						OssMSG response;
						response.mtype = processTable[blockedIndex].pid;
						response.pid = processTable[blockedIndex].pid;
						response.resourceID = resourceID;
						response.quantity = remainingRequest;
						msgsnd(msgid, &response, sizeof(OssMSG) - sizeof(long), 0);
				    
						grantedAfterWait++; // Update for requests that will be granted after being blocked.

						if (linesWritten < 10000 && verbose) {
							fprintf(file, "OSS: Unblocked P%d with R%d (%d units) at %u:%u\n", processTable[blockedIndex].pid, resourceID, remainingRequest, clock->seconds, clock->nanoseconds);
                                                        printf("OSS: Unblocked P%d with R%d (%d units) at %u:%u\n", processTable[blockedIndex].pid, resourceID, remainingRequest, clock->seconds, clock->nanoseconds);
							linesWritten++;
				    		}

						resourceTable[resourceID].requestQueue[head] = -1; // Mark this queue slot as empty since request is granted 
					}

					head = (head + 1) % MAX_PCB; // Move forward to next blocked process 
				}

				resourceTable[resourceID].head = head; // Update queue head.
			}
		}
		
		// Check if 0.5 seconds have passed.
		if ((clock->seconds > lastPrintSec) || (clock->seconds == lastPrintSec && (clock->nanoseconds - lastPrintNano >= 500000000))) {
		       	if (linesWritten < 10000) {
				// Print header for resource and pcb table	
				fprintf(file, "OSS: Resource and Process Table at %u:%u\n", clock->seconds, clock->nanoseconds);
				printf("OSS: Resource and Process Table at %u:%u\n", clock->seconds, clock->nanoseconds);
				linesWritten++;

				// Resource Table
				fprintf(file, "Available Resources:\n");
				printf("Available Resources:\n");

				for (int i = 0; i < NUM_RESOURCES; i++) {
			    		fprintf(file, "R%d: %d/%d\n", i, resourceTable[i].availableInstances, resourceTable[i].totalInstances);					           
	       				printf("R%d: %d/%d\n", i, resourceTable[i].availableInstances, resourceTable[i].totalInstances);
			    		linesWritten++;
				}
				
				// Process Table 
				fprintf(file, "Process Table:\n");
				printf("Process Table:\n");

				for (int i = 0; i < MAX_PCB; i++) {
					if (processTable[i].occupied) {
						fprintf(file, "P:%d ", processTable[i].pid);						                                   
				   		printf("P:%d ", processTable[i].pid);
						
						for (int j = 0; j < NUM_RESOURCES; j++) {
							fprintf(file, "R%d=%d ", j, processTable[i].resourceAllocated[j]);
							printf("R%d=%d ", j, processTable[i].resourceAllocated[j]);
						}

						fprintf(file, "\n");						                                             
						printf("\n");
						linesWritten++;
					}
			}
			// Update last printed time.
			lastPrintSec = clock->seconds;
			lastPrintNano = clock->nanoseconds;
		}
		// Check if lines written in log file exceeds limit.
		if (linesWritten >= 10000) {
			fprintf(file, "OSS: Log limit of 10,000 lines reached.\n");
			printf("OSS: Log limit of 10,000 lines reached.\n");
			break;
	    	}
	}

	double averageTerminations = 0.0;
	if (deadlockProcesses > 0) {
		averageTerminations = ((double) deadlockTerminations / deadlockProcesses) * 100;
	}

	// Log statistics statistics
	fprintf(file, "\nSIMULATION SUMMARY\n");
	fprintf(file, "Total Requests: %d\n", totalRequests);
	fprintf(file, "Granted Instantly: %d\n", grantedInstantly);
	fprintf(file, "Granted After Wait: %d\n", grantedAfterWait);
	fprintf(file, "Deadlock Detection Runs: %d\n", deadlockDetectedRun);
	fprintf(file, "Total Deadlocked Processes Detected: %d\n", deadlockProcesses);
	fprintf(file, "Processes Terminated due to Deadlock: %d\n", deadlockTerminations);
	fprintf(file, "Processes Terminated Normally: %d\n", terminations);	
	fprintf(file, "%% of Deadlocked Processes Terminated: %.2f%%\n", averageTerminations);	
	
	// Print statistics
        printf("\nSIMULATION SUMMARY\n");
        printf("Total Requests: %d\n", totalRequests);
        printf("Granted Instantly: %d\n", grantedInstantly);
        printf("Granted After Wait: %d\n", grantedAfterWait);
        printf("Deadlock Detection Runs: %d\n", deadlockDetectedRun);
        printf("Total Deadlocked Processes Detected: %d\n", deadlockProcesses);
        printf("Processes Terminated due to Deadlock: %d\n", deadlockTerminations);
        printf("Processes Terminated Normally: %d\n", terminations);
        printf("%% of Deadlocked Processes Terminated: %.2f%%\n", averageTerminations);

	// Detach shared memory
    	if (shmdt(clock) == -1) {
        	printf("Error: OSS Shared memory detachment failed \n");
		exit(1);
    	}
	
	// Detach shared memory for resource table
	if (shmdt(resourceTable) == -1) {
                printf("Error: OSS Shared memory detachment failed \n");
                exit(1);
        }


    	// Remove shared memory
    	if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        	printf("Error: Removing memory failed \n");
		exit(1);
    	}

	// Remove shared memory for resource table
	if (shmctl(shmResourceID, IPC_RMID, NULL) == -1) {
                printf("Error: Removing memory failed \n");
                exit(1);
        }

	// Remove message queue
	if (msgctl(msgid, IPC_RMID, NULL) == -1) {
		printf("Error: Removing msg queue failed. \n");
		exit(1);
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
	
	// Cleanup resource descriptor
	int shmResourceID = shmget(RESOURCE_KEY, sizeof(ResourceDesc) * NUM_RESOURCES, 0666);
	if (shmResourceID != -1) {
	    	ResourceDesc *resourceTable = (ResourceDesc *)shmat(shmResourceID, NULL, 0);
	    	if (resourceTable != (void *)-1) {
			shmdt(resourceTable);
	    	}
	    	shmctl(shmResourceID, IPC_RMID, NULL);
	}


	exit(1);
}

void help() {
	printf("Usage: ./oss [-h] [-n proc] [-s simul] [-i interval] [-f logfile] [-v]\n");
    	printf("Options:\n");
    	printf("-h 	      Show this help message and exit.\n");
    	printf("-n proc       Total number of user processes to launch (default: 40).\n");
    	printf("-s simul      Maximum number of simultaneous processes (max: 18).\n");
    	printf("-i interval   Time interval (ms) between process launches (default: 500).\n");
	printf("-f logfile    Name of the log file to write output (default: oss.log).\n");
    	printf("-v            Enable verbose output to both screen and file.\n");
}
