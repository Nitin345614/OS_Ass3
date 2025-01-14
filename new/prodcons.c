#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>  
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "prodcons.h"

static ITEM buffer[BUFFER_SIZE];
static int in = 0;
static int out = 0;
static int count = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;

static void rsleep(int t);
static ITEM get_next_item(void);

/* producer thread */
static void *producer(void *arg) {
    while (true) {
        ITEM item = get_next_item();
        if (item == NROF_ITEMS) break;

        rsleep(100);

        pthread_mutex_lock(&buffer_mutex);
        while (count == BUFFER_SIZE) {
            pthread_cond_wait(&buffer_not_full, &buffer_mutex);
        }

        buffer[in] = item;
        in = (in + 1) % BUFFER_SIZE;
        count++;

        pthread_cond_signal(&buffer_not_empty);
        pthread_mutex_unlock(&buffer_mutex);
    }
    return NULL;
}

/* consumer thread */
static void *consumer(void *arg) {
    while (true) {
        pthread_mutex_lock(&buffer_mutex);
        while (count == 0) {
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
        }

        ITEM item = buffer[out];
        out = (out + 1) % BUFFER_SIZE;
        count--;

        pthread_cond_signal(&buffer_not_full);
        pthread_mutex_unlock(&buffer_mutex);

        if (item == NROF_ITEMS) break;

        printf("%d\n", item);
        rsleep(100);
    }
    return NULL;
}

int main(void) {
    pthread_t producer_threads[NROF_PRODUCERS];
    pthread_t consumer_thread;

    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_create(&producer_threads[i], NULL, producer, NULL);
    }
    pthread_create(&consumer_thread, NULL, consumer, NULL);

    for (int i = 0; i < NROF_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    pthread_join(consumer_thread, NULL);

    return 0;
}

static void rsleep(int t) {
    static bool first_call = true;
    if (first_call) {
        srandom(time(NULL));
        first_call = false;
    }
    usleep(random() % t);
}

static ITEM get_next_item(void) {
    static pthread_mutex_t job_mutex = PTHREAD_MUTEX_INITIALIZER;
    static bool jobs[NROF_ITEMS + 1] = { false };
    static int counter = 0;
    ITEM found;

    pthread_mutex_lock(&job_mutex);
    counter++;
    if (counter > NROF_ITEMS) {
        found = NROF_ITEMS;
    } else {
        if (counter < NROF_PRODUCERS) {
            found = (random() % (2 * NROF_PRODUCERS)) % NROF_ITEMS;
        } else {
            found = counter - NROF_PRODUCERS;
            if (jobs[found]) {
                found = (counter + (random() % NROF_PRODUCERS)) % NROF_ITEMS;
            }
        }
        if (jobs[found]) {
            found = 0;
            while (jobs[found]) {
                found++;
            }
        }
    }
    jobs[found] = true;
    pthread_mutex_unlock(&job_mutex);
    return found;
}