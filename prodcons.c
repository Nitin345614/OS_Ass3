/* 
 * Operating Systems  (2INC0)  Practical Assignment.
 * Condition Variables Application.
 *
 * STUDENT_NAME_1 (STUDENT_NR_1)
 * STUDENT_NAME_2 (STUDENT_NR_2)
 *
 * Grading:
 * Students who hand in clean code that fully satisfies the minimum requirements will get an 8. 
 * Extra steps can lead to higher marks because we want students to take the initiative.
 */
 
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>  
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>


#include "prodcons.h"

//A circular buffer can be used to create a FIFO queue
static ITEM buffer[BUFFER_SIZE];

//in/out are used to index the buffer for data input/output
int in = 0;
int out = 0;
int count = 0;
int cv = 0;
int fin = 0;

pthread_mutex_t  in_mutex   = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  out_mutex   = PTHREAD_COND_INITIALIZER;
pthread_cond_t cv_mutex = PTHREAD_COND_INITIALIZER;

static void rsleep (int t);	    // already implemented (see below)
static ITEM get_next_item (void);   // already implemented (see below)

/* producer thread */
static void * 
producer ()
{
	ITEM curr_job = 0;
	
    while (true)
    {
        // TODO: 
        // * get the new item
        
        ITEM curr_job = get_next_item();
		//fprintf(stderr,"producer0: %d\n",curr_job);
		
        rsleep (100);	// simulating all kind of activities...
		
		pthread_mutex_lock(&in_mutex);
		
		
		
		//The ascending jobs condition is enforced here
		//Unlocking on the in_mutex allows other producers to unblock
		//if the current one can't do anything	
		//if curr_job is lower than cv, the producers must wait the 
		//consumer to consume the whole buffer
		while(curr_job<cv){
			pthread_cond_wait (&cv_mutex, &in_mutex);
			}
		//fprintf(stderr,"producer1: %d\n",curr_job);	
		//Once the current producer can do things, it blocks other
		//producers
		//pthread_mutex_lock(&in_mutex);
		//fprintf(stderr,"producer2: %d\n",cv);
		//Put jobs into the buffer and update buffer trackers
		buffer[in] = curr_job;
		in = (in + 1)%BUFFER_SIZE;
		count = count + 1;
		
		//The condition variable can be used like this
		//The value of the condition variable is the same as the last
		//job id put into the buffer
		//The value of the condition variable is set to the negative
		//of the latest job id if the buffer is full
		cv = curr_job * (1 - 2*(count==BUFFER_SIZE));
		
		//Signals to consumer that new item has been added for
		//consuming
		pthread_cond_signal (&out_mutex);
		
		pthread_mutex_unlock(&in_mutex);
		
		
	// TODO:
	      // * put the item into buffer[]
	//
        // follow this pseudocode (according to the ConditionSynchronization lecture):
        //      mutex-lock;
        //      while not condition-for-this-producer
        //          wait-cv;
        //      critical-section;
        //      possible-cv-signals;
        //      mutex-unlock;
        //
        // (see condition_test() in condition_basics.c how to use condition variables)
    }
    //fprintf(stderr,"producer2");
	return (NULL);
}

/* consumer thread */
static void * 
consumer ()
{
	ITEM curr_job = 0;
    while (curr_job<NROF_ITEMS)
    {
		//fprintf(stderr,"consumer0: %d\n",curr_job);
        // TODO: 
	      // * get the next item from buffer[]
	      // * print the number to stdout
        //
        
        pthread_mutex_lock(&in_mutex);
        
        //If the buffer is empty, the consumer thread waits for 
        //the out mutex (so for producers to signal completion)
        //while releasing the in_mutex (so allowing producers to unblock)
        while(count==0){
			pthread_cond_wait (&out_mutex, &in_mutex);
			}
		
        //pthread_mutex_lock(&in_mutex);
        
        //fprintf(stderr,"consumer2: %d\n",cv);
		
		//Put jobs into the buffer and update buffer trackers
		curr_job = buffer[out];
		printf("%d\n", curr_job);
		out = (out + 1)%BUFFER_SIZE;
		count = count - 1;
		
		//If buffer has been emptied, reset cv
		//If cv was negative (full buffer)
		//make it positve again (there is now space
		//in buffer)
		cv = cv*(count>0)*(1 - 2*(cv<0));
		
		//Signal to producers to try again
		pthread_cond_signal (&cv_mutex);
        
        pthread_mutex_unlock(&in_mutex);
        
        // follow this pseudocode (according to the ConditionSynchronization lecture):
        //      mutex-lock;
        //      while not condition-for-this-consumer
        //          wait-cv;
        //      critical-section;
        //      possible-cv-signals;
        //      mutex-unlock;
		
        rsleep (100);		// simulating all kind of activities...
    }
    //fprintf(stderr,"consumer2");
	return (NULL);
}

int main (void)
{
    // TODO: 
    // * startup the producer threads and the consumer thread
    // * wait until all threads are finished  
    
    pthread_t producer_threads[NROF_PRODUCERS];
    pthread_t consumer_thread;
	for (int i = 0; i < NROF_PRODUCERS; i++){
		pthread_create(&producer_threads[i], NULL, producer, NULL);
	}
	
	pthread_create(&consumer_thread, NULL, consumer, NULL);
	
	//Waits for the consumer to terminate
	//int status = -1;
	//while(status==-1) 
	//{
	//	waitpid(consumer_thread, &status,WCONTINUED);
	//}
	
	pthread_join (consumer_thread, NULL);
	
	//fprintf(stderr,"main\n");
	for (int i = 0; i < NROF_PRODUCERS; i++){
		pthread_cancel(producer_threads[i]);
	}
	
	
	pthread_mutex_destroy(&in_mutex);
	pthread_cond_destroy(&out_mutex);
	pthread_cond_destroy(&cv_mutex);
    
    return (0);
}

/*
 * rsleep(int t)
 *
 * The calling thread will be suspended for a random amount of time between 0 and t microseconds
 * At the first call, the random generator is seeded with the current time
 */
static void 
rsleep (int t)
{
    static bool first_call = true;
    
    if (first_call == true)
    {
        srandom (time(NULL));
        first_call = false;
    }
    usleep (random () % t);
}


/* 
 * get_next_item()
 *
 * description:
 *	thread-safe function to get a next job to be executed
 *	subsequent calls of get_next_item() yields the values 0..NROF_ITEMS-1 
 *	in arbitrary order 
 *	return value NROF_ITEMS indicates that all jobs have already been given
 * 
 * parameters:
 *	none
 *
 * return value:
 *	0..NROF_ITEMS-1: job number to be executed
 *	NROF_ITEMS:	 ready
 */
static ITEM
get_next_item(void)
{
    static pthread_mutex_t  job_mutex   = PTHREAD_MUTEX_INITIALIZER;
    static bool    jobs[NROF_ITEMS+1] = { false }; // keep track of issued jobs
    static int     counter = 0;    // seq.nr. of job to be handled
    ITEM           found;          // item to be returned
	
	/* avoid deadlock: when all producers are busy but none has the next expected item for the consumer 
	 * so requirement for get_next_item: when giving the (i+n)'th item, make sure that item (i) is going to be handled (with n=nrof-producers)
	 */
	pthread_mutex_lock (&job_mutex);

	counter++;
	if (counter > NROF_ITEMS)
	{
	    // we're ready
	    found = NROF_ITEMS;
	}
	else
	{
	    if (counter < NROF_PRODUCERS)
	    {
	        // for the first n-1 items: any job can be given
	        // e.g. "random() % NROF_ITEMS", but here we bias the lower items
	        found = (random() % (2*NROF_PRODUCERS)) % NROF_ITEMS;
	    }
	    else
	    {
	        // deadlock-avoidance: item 'counter - NROF_PRODUCERS' must be given now
	        found = counter - NROF_PRODUCERS;
	        if (jobs[found] == true)
	        {
	            // already handled, find a random one, with a bias for lower items
	            found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
	        }    
	    }
	    
	    // check if 'found' is really an unhandled item; 
	    // if not: find another one
	    if (jobs[found] == true)
	    {
	        // already handled, do linear search for the oldest
	        found = 0;
	        while (jobs[found] == true)
            {
                found++;
            }
	    }
	}
    	jobs[found] = true;
			
	pthread_mutex_unlock (&job_mutex);
	return (found);
}



