#ifndef OSS_H
#define OSS_H

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h> // For shared memory


#define SHM_KEY 856050
#define MSG_KEY 875010
#define RESOURCE_KEY 886121
#define MAX_PCB 20
#define NUM_RESOURCES 5
#define INSTANCES_PER_RESOURCE 10

// Author: Dat Nguyen
// oss.h is a header file that holds our structures and some of our constant definitions, useful for cleanliness of oss.c

// Using a structure for our simulated clock, storing seconds and nanoseconds.
typedef struct SimulatedClock {
       unsigned int seconds;
       unsigned int nanoseconds;
} SimulatedClock;

typedef struct PCB { // PCB structure, subject to change
        int occupied;
        pid_t pid;
        int startSeconds;
        int startNano;
	int resourceAllocated[NUM_RESOURCES]; // Track resources
	int maxResources[NUM_RESOURCES]; // Max resource amount for process
	int blocked; // See if process is waiting for resource 
} PCB;

typedef struct ResourceDesc { // Resource structure, each object represents a resource.
    int totalInstances; // How many instances of this resource exist
    int availableInstances; // Free instances 
    int resourceAllocated[MAX_PCB]; // How many instances each process is holding
    int requestQueue[MAX_PCB]; // What processes are waiting for this resource
    int head; // Start of resource queue
    int tail; // End of resource queue
} ResourceDesc;

typedef struct OssMSG { // Message system
	long mtype;
	pid_t pid;
	int resourceID;
	int quantity;
} OssMSG;

#endif 

