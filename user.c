#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <signal.h>
#include <time.h>
#include "oss.h"

#define NANO_TO_SEC 1000000000
#define BOUND 500000000 // 0.5 second bound to request/release
#define REQUEST_PROBABILITY 70 // 70% chance to request, 30% to release

int main(int argc, char* argv[]) {
	if (argc != 2) {
		fprintf(stderr, "user_proc: Invalid arguments. Expected PCB index.\n");
		exit(1);
    	}

	int pcbIndex = atoi(argv[1]);
	
	// Attach to simulated clock
    	int clockShmId = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);
    	SimulatedClock *clock = (SimulatedClock *)shmat(clockShmId, NULL, 0);

    	// Attach to resource table
    	int resourceShmId = shmget(RESOURCE_KEY, sizeof(ResourceDesc) * NUM_RESOURCES, 0666);
    	ResourceDesc *resourceTable = (ResourceDesc *)shmat(resourceShmId, NULL, 0);

    	// Message queue
    	int msgid = msgget(MSG_KEY, 0666);

    	// Local resource tracking
    	int resourceHeld[NUM_RESOURCES] = {0};

	return 0;
}
