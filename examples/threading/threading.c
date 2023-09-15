#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...) printf("threading DEBUG: " msg "\n" , ##__VA_ARGS__)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    struct thread_data *tdata = (struct thread_data *)thread_param;
    int s = 0;

    usleep(tdata->wait_to_obtain_ms * 1000);
    s = pthread_mutex_lock(tdata->mutex);
    if (s != 0){
        perror("pthread_mutex_lock");
        tdata->thread_complete_success = false;
        return thread_param;
    }

    usleep(tdata->wait_to_release_ms * 1000);
    s = pthread_mutex_unlock(tdata->mutex);
    if (s != 0){
        perror("pthread_mutex_unlock");
        tdata->thread_complete_success = false;
        return thread_param;
    }

    tdata->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *tdata;
    int s = 0;

    tdata = calloc(1, sizeof(struct thread_data));
    if (tdata == NULL){
        perror("calloc");
        return false;
    }

    tdata->mutex = mutex;
    tdata->wait_to_obtain_ms = wait_to_obtain_ms;
    tdata->wait_to_release_ms = wait_to_release_ms;

    DEBUG_LOG("Main thread, PID %d TID %d", getpid(), (pid_t)syscall(SYS_gettid));

    s = pthread_create(thread, NULL, threadfunc, tdata);
    if (s != 0){
        ERROR_LOG("Failed to create thread");
        perror("pthread_create");
        free(tdata);
        return false;
    }

    return true;
}
