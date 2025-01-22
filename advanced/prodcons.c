/*
 * Operating Systems  (2INC0)  Practical Assignment.
 * Condition Variables Application.
 *
 * Nitin Singhal (1725963)
 * Daniel Tyukov (1819283)
 * Ben Lentschig (1824805)
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
#include <pthread.h>
#include <time.h>

#include "prodcons.h"

/*
 * Global circular buffer plus bookkeeping
 */
static ITEM buffer[BUFFER_SIZE];
// next free position in circular buffer
static int  in   = 0;
// position of next item to consume
static int  out  = 0;
// how many items in the buffer right now
static int  count= 0;

/*
 * "next_to_produce" = item number that must be produced *next* in ascending order
 * Each producer can only produce its item when item == next_to_produce.
 */
static int  next_to_produce = 0;

/*
 * We will keep track of how many items are consumed so the consumer
 * knows when to stop.
 */
static int  items_consumed = 0;

/*
 * For the advanced solution, each producer holds exactly one item at
 * a time (which it got from get_next_item()).
 * We store that item in producer_items[id].
 * If producer_done[id] is true, that producer has no more items to fetch.
 */
static ITEM producer_items[NROF_PRODUCERS];
static bool producer_has_item[NROF_PRODUCERS];
static bool producer_done[NROF_PRODUCERS];

/*
 * Condition variables and mutex:
 *   - mutex_global: Protects all shared state (buffer, next_to_produce, etc.)
 *   - cond_not_full: for producers to wait if buffer is full
 *   - cond_not_empty: for consumer to wait if buffer is empty
 *   - cond_producer[i]: for producer i to wait if it cannot yet produce
 *                       (i.e., it does not hold the next item needed in ascending order)
 */
static pthread_mutex_t mutex_global       = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond_not_full      = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  cond_not_empty     = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  cond_producer[NROF_PRODUCERS];

/*
 * In an "optimized" approach, we expect signals and no broadcasts.
 */
static int signal_count    = 0;
static int broadcast_count = 0;

/* forward declarations */
static void *producer(void *arg);
static void *consumer(void *arg);
static void  rsleep(int t);
static ITEM  get_next_item(void);

int main(void)
{
    pthread_t prod_thread_id[NROF_PRODUCERS];
    pthread_t cons_thread_id;

    // Initialize per-producer condition variables
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_cond_init(&cond_producer[i], NULL);
        // the default placeholder basically..
        producer_items[i]     = NROF_ITEMS;
        producer_has_item[i]  = false;
        producer_done[i]      = false;
    }

    // Create producer threads
    // We'll pass each producer its own ID (0..NROF_PRODUCERS-1)
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&prod_thread_id[i], NULL, producer, id);
    }

    // Create consumer thread
    pthread_create(&cons_thread_id, NULL, consumer, NULL);

    // Wait for consumer to finish
    pthread_join(cons_thread_id, NULL);

    // Once consumer is done, we know all items are processed
    // The producers might still be blocked waiting for next_to_produce
    // or waiting for get_next_item() returning NROF_ITEMS. We should
    // cancel them or join them after they've discovered they have no more items.
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_cancel(prod_thread_id[i]);
        pthread_join(prod_thread_id[i], NULL);
    }

    // Clean up
    pthread_mutex_destroy(&mutex_global);
    pthread_cond_destroy(&cond_not_full);
    pthread_cond_destroy(&cond_not_empty);
    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_cond_destroy(&cond_producer[i]);
    }

    return 0;
}

/*
 * producer()
 *
 * Each producer repeatedly:
 *  - obtains an item from get_next_item()
 *  - if that item == NROF_ITEMS, it means no more real items => stop
 *  - otherwise, sleep a little (simulate fetching)
 *  - wait until 'my item' == next_to_produce
 *  - wait until buffer not full
 *  - produce into buffer, increment next_to_produce,
 *  - signal consumer if buffer was empty
 *  - signal the one producer that holds the next item (if any)
 */
static void *producer(void *arg)
{
    int my_id = *(int*)arg;
    // finished with allocated id
    free(arg);

    while (true)
    {
        // 1) get next item
        ITEM new_item = get_next_item();
        if (new_item == NROF_ITEMS) {
            // no more real items => we're done
            pthread_mutex_lock(&mutex_global);
            producer_done[my_id] = true;
            pthread_mutex_unlock(&mutex_global);
            break;
        }

        // store it in our "hand"
        producer_items[my_id]    = new_item;
        producer_has_item[my_id] = true;

        // simulate "producer" outside critical section
        rsleep(100);

        // 2) Now produce into the buffer in ascending order
        pthread_mutex_lock(&mutex_global);

        // Wait until this is indeed the next item needed (avoid broadcast)
        while ((int)producer_items[my_id] != next_to_produce) {
            // Wait on my own condition variable
            pthread_cond_wait(&cond_producer[my_id], &mutex_global);
        }

        // Now wait if the buffer is full
        while (count == BUFFER_SIZE) {
            pthread_cond_wait(&cond_not_full, &mutex_global);
        }

        // Put item in buffer
        buffer[in] = producer_items[my_id];
        in         = (in + 1) % BUFFER_SIZE;
        count++;
        // this item is now placed
        producer_has_item[my_id] = false;

        // Let consumer know there is something in the buffer
        if (count == 1) {
            // buffer was empty, so consumer must be woken
            signal_count++;
            pthread_cond_signal(&cond_not_empty);
        }

        // We have produced item "next_to_produce", so increment
        next_to_produce++;

        // Now wake up whichever producer has the next item (if any)
        // Because exactly one item is 'next', we only need to wake exactly one
        // We find a producer j that is holding that item
        for (int j = 0; j < NROF_PRODUCERS; j++) {
            if (producer_has_item[j] &&
                producer_items[j] == next_to_produce) {
                // signal exactly that producer j
                signal_count++;
                pthread_cond_signal(&cond_producer[j]);
                break;
            }
        }

        // Also, let the thread exit
        pthread_mutex_unlock(&mutex_global);

        // We repeat until get_next_item() yields NROF_ITEMS
    }

    return NULL;
}

/*
 * consumer()
 *
 * The consumer repeatedly:
 *  - waits if the buffer is empty
 *  - consumes the next item from 'out'
 *  - prints it
 *  - signals a producer if the buffer was full
 *  - stops when we have consumed all NROF_ITEMS items
 */
static void *consumer(void *arg)
{
    (void)arg;

    while (true)
    {
        pthread_mutex_lock(&mutex_global);

        while (count == 0) {
            pthread_cond_wait(&cond_not_empty, &mutex_global);
        }

        // consume next item
        ITEM item = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        count--;

        // let producers know there's space
        if (count == BUFFER_SIZE - 1) {
            // buffer was full, now there's space for at least one
            signal_count++;
            pthread_cond_signal(&cond_not_full);
        }

        // update how many we've consumed
        items_consumed++;

        pthread_mutex_unlock(&mutex_global);

        // only printing the real items
        if (item < NROF_ITEMS) {
            printf("%d\n", item);
        }

        // sleep to simulate packaging
        rsleep(100);

        // if we've consumed the total # of real items, we can stop
        if (items_consumed >= NROF_ITEMS) {
            break;
        }
    }

    return NULL;
}

/*
 * rsleep(int t)
 *
 * Suspends the calling thread for a random amount of time between
 * 0 and t microseconds.  Seeds the RNG at first call.
 */
static void rsleep(int t)
{
    static bool first_call = true;

    if (first_call) {
        srandom(time(NULL));
        first_call = false;
    }
    usleep(random() % t);
}

/*
 * get_next_item()
 *
 * Thread-safe function to get a next job to be executed.
 * Subsequent calls yield values 0..NROF_ITEMS-1 in *arbitrary order*.
 * Return value == NROF_ITEMS => all items have been handed out already.
 */
static ITEM get_next_item(void)
{
    static pthread_mutex_t  job_mutex = PTHREAD_MUTEX_INITIALIZER;
    // keep track of issued jobs
    static bool    jobs[NROF_ITEMS+1] = { false };
    // seq.nr. of job to be handled
    static int     counter = 0;
    ITEM           found;

    pthread_mutex_lock(&job_mutex);

    counter++;
    if (counter > NROF_ITEMS) {
        // all items already handed out
        found = NROF_ITEMS;
    } else {
        if (counter < NROF_PRODUCERS) {
            // for the first n-1 items: any job can be given
            // but we bias the lower items
            found = (random() % (2*NROF_PRODUCERS)) % NROF_ITEMS;
        } else {
            // deadlock avoidance: item 'counter - NROF_PRODUCERS' must be given now
            found = counter - NROF_PRODUCERS;
            if (jobs[found] == true) {
                // already handled, find a random one (bias to lower items)
                found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
            }
        }
        // ensure 'found' is an unhandled item
        if (jobs[found] == true) {
            // do linear search for the oldest that is unhandled
            found = 0;
            while (jobs[found] == true) {
                found++;
            }
        }
    }
    if (found < NROF_ITEMS) {
        jobs[found] = true;
    }

    pthread_mutex_unlock(&job_mutex);
    return found;
}
