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
#define TERMINATION_PROBABILITY 10

int main(int argc, char* argv[]) {
	
	// Attach to simulated clock
    	int shmid = shmget(SHM_KEY, sizeof(SimulatedClock), 0666);
    	SimulatedClock *clock = (SimulatedClock *)shmat(shmid, NULL, 0);
	if (shmid == -1) { // If shmid is -1 as a result of shmget failing and returning -1, error message will print.
		printf("Error: User shmget failed. \n");
        	exit(1);
	}
	if (clock == (void *)-1) { // if shmat, the attaching shared memory function, fails, it returns an invalid memory address.
		printf("Error: User shared memory attachment failed. \n");
		exit(1);
	}

    	// Attach to resource table
    	int shmResourceID = shmget(RESOURCE_KEY, sizeof(ResourceDesc) * NUM_RESOURCES, 0666);
    	if (shmResourceID == -1) { // Error message in case creating shm fails.
    		printf("OSS Error: Failed to allocate shared memory for resource table");
    		exit(1);
	}
	
	ResourceDesc *resourceTable = (ResourceDesc *)shmat(shmResourceID, NULL, 0);
	if (resourceTable == (void *) -1) { // Error message in case of attatch fail.
    		printf("Error: OSS Failed to attach shared memory for resource table");
		exit(1);
	}

    	// Message queue
    	int msgid = msgget(MSG_KEY, 0666);
	if (msgid == -1) {
                printf("Error: OSS msgget failed. \n");
                exit(1);
        }

    	// Local resource tracking
    	int resourceHeld[NUM_RESOURCES] = {0};

	srand(getpid());
    	unsigned int lastCheck = clock->seconds * NANO_TO_SEC + clock->nanoseconds;
    	unsigned int startTime = lastCheck;
	
       	while (1) {
	       	unsigned int currentTime = clock->seconds * NANO_TO_SEC + clock->nanoseconds;
		if ((currentTime - startTime) >= NANO_TO_SEC) { // Run at least 1 second
	    		int terminateCheck = rand() % 100;
	    		if (terminateCheck < TERMINATION_PROBABILITY) { // 10% chance to terminate
				for (int i = 0; i < NUM_RESOURCES; i++) {
		    			if (resourceHeld[i] > 0) {
						OssMSG releaseMsg;
						releaseMsg.mtype = 1;
						releaseMsg.pid = getpid();
						releaseMsg.resourceID = i;
						releaseMsg.quantity = -1; // negative indicates release
						msgsnd(msgid, &releaseMsg, sizeof(OssMSG) - sizeof(long), 0);
		    			}
				}
				break;
	    		}
		}
		
		if ((currentTime - lastCheck) >= BOUND) {
	    		lastCheck = currentTime;
	    
			int action = rand() % 100;
	    		int resourceID = rand() % NUM_RESOURCES;
	    
			if (action < REQUEST_PROBABILITY) {
				if (resourceHeld[resourceID] == 0) {
		    			OssMSG request;
		    			request.mtype = 1;
		    			request.pid = getpid();
		    			request.resourceID = resourceID;
		    			request.quantity = 1;
		    			msgsnd(msgid, &request, sizeof(OssMSG) - sizeof(long), 0);
		    
					OssMSG response;
		    			msgrcv(msgid, &response, sizeof(OssMSG) - sizeof(long), getpid(), 0);
		    			if (response.quantity > 0) {
						resourceHeld[resourceID] += response.quantity;
		    			}
				}
	    		} else {
				if (resourceHeld[resourceID] > 0) {
		    			OssMSG release;
		    			release.mtype = 1;
		    			release.pid = getpid();
		    			release.resourceID = resourceID;
		    			release.quantity = -1; // negative indicates release
		    			msgsnd(msgid, &release, sizeof(OssMSG) - sizeof(long), 0);
		    			resourceHeld[resourceID] = 0;
				}
	    		}
		}
	}

	shmdt(clock);
	shmdt(resourceTable);

	return 0;
}
