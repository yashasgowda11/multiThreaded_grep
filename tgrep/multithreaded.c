#include <stdio.h>    // Standard I/O functions
#include <stdlib.h>   // Standard library for memory allocation, process control, conversions, etc.
#include <sys/types.h> // System data types
#include <unistd.h>   // POSIX operating system API
#include <dirent.h>   // Directory entry
#include <pthread.h>  // POSIX threads
#include <semaphore.h> // POSIX semaphores
#include <fcntl.h>    // File control options
#include <string.h>   // String operations

// Struct for each node in the task queue
struct task_queue_node {
    char *associated_path;       // Path associated with this node
    struct task_queue_node *next_node; // Pointer to the next node
};

// Struct for the task queue itself
struct task_queue {
    sem_t *head_lock;            // Semaphore for locking the head of the queue
    sem_t *tail_lock;            // Semaphore for locking the tail of the queue
    struct task_queue_node *head; // Head of the queue
    struct task_queue_node *tail; // Tail of the queue
};

// Function prototypes
void task_queue_initialize(struct task_queue *kyu); // Initialize the task queue
void task_queue_free(struct task_queue *kyu);       // Free the task queue
int task_queue_dequeue(struct task_queue *kyu, char *dequeued_path); // Dequeue a path from the queue
void task_queue_enqueue(struct task_queue *kyu, char *new_path);      // Enqueue a path to the queue
int all_done_checker();                        // Check if all threads are done

// Global variables
struct task_queue *queue;     // Pointer to the task queue
char *search_string;          // Search string for file content search
int num_workers;              // Number of worker threads
int *thread_statuses;         // Array of thread statuses
sem_t *thread_statuses_lock;  // Semaphore for locking the thread status array

// Flags option (1 means in use, 0 otherwise) 
int c_option = 0; // This prints only a count of the lines that match a pattern
int h_option = 0; // Display the matched lines, but do not display the filenames
int i_option = 0; // Ignores, case for matching
int l_option = 0; // Displays list of filenames containing text only.
int n_option = 0; // Display the matched lines and their line numbers
int v_option = 0; // This prints out all the lines that do not matches the pattern

// Initializes the task queue
void task_queue_initialize(struct task_queue *kyu) {
    struct task_queue_node *placeholder = malloc(sizeof(struct task_queue_node)); // Allocate memory for the dummy node
    placeholder->associated_path = malloc(sizeof(char) * 300); // Allocate memory for the path
    placeholder->associated_path[0] = '\0'; // Initialize path with an empty string
    placeholder->next_node = NULL; // Set next node to NULL
    kyu->head = placeholder; // Set head to the placeholder
    kyu->tail = placeholder; // Set tail to the placeholder
    kyu->head_lock = sem_open("/headLock", O_CREAT, 0644, 1); // Open a new semaphore for the head
    kyu->tail_lock = sem_open("/tailLock", O_CREAT, 0644, 1); // Open a new semaphore for the tail
}

// Frees the task queue
void task_queue_free(struct task_queue *kyu) {
    sem_close(kyu->head_lock); // Close the head semaphore
    sem_close(kyu->tail_lock); // Close the tail semaphore
    sem_unlink("/headLock"); // Unlink the head semaphore
    sem_unlink("/tailLock"); // Unlink the tail semaphore
    free(kyu->head->associated_path); // Free the path of the head node
    free(kyu->head); // Free the head node
    free(kyu); // Free the queue structure
}

// Checks if all worker threads are done
int all_done_checker() {
    for (int i = 0; i < num_workers; i++) { // Iterate over all worker threads
        if (thread_statuses[i] == 1) { // If any thread is still working
            return -1; // Return -1 indicating threads are still working
        }
    }
    return 0; // Return 0 indicating all threads are done
}

// Enqueues a new path to the task queue
void task_queue_enqueue(struct task_queue *kyu, char *new_path) {
    struct task_queue_node *new_node = malloc(sizeof(struct task_queue_node)); // Allocate memory for a new node
    new_node->associated_path = strdup(new_path); // Duplicate the path and assign it to the node
    new_node->next_node = NULL; // Set the next node to NULL

    sem_wait(kyu->tail_lock); // Lock the tail semaphore
    kyu->tail->next_node = new_node; // Set the next node of the tail to the new node
    kyu->tail = new_node; // Update the tail to the new node
    sem_post(kyu->tail_lock); // Unlock the tail semaphore
}

// Dequeues a path from the task queue
int task_queue_dequeue(struct task_queue *kyu, char *dequeued_path) {
    sem_wait(kyu->head_lock); // Lock the head semaphore
    struct task_queue_node *temp_node = kyu->head; // Temporarily store the head node
    struct task_queue_node *new_head = temp_node->next_node; // Get the next node of the head

    if (new_head == NULL) { // If the queue is empty
        sem_post(kyu->head_lock); // Unlock the head semaphore
        return -1; // Return -1 indicating queue is empty
    }

    strcpy(dequeued_path, new_head->associated_path); // Copy the path from the next node to dequeued_path
    kyu->head = new_head; // Move the head pointer to the next node
    sem_post(kyu->head_lock); // Unlock the head semaphore

    free(temp_node->associated_path); // Free the path of the old head node
    free(temp_node); // Free the old head node
    return 0; // Return 0 indicating success
}

// Worker thread behavior function
void worker_behavior(int *id) {
    int workerID = *id; // Store the worker ID
    char *curr_task = malloc(sizeof(char) * 300); // Allocate memory for the current task path

    while(1) {
        sem_wait(thread_statuses_lock); // Lock the thread statuses
        int status = task_queue_dequeue(queue, curr_task); // Attempt to dequeue a task
        if (status < 0) { // If the dequeue failed (queue is empty)
            if (all_done_checker() < 0) { // Check if all threads are done
                sem_post(thread_statuses_lock); // Unlock the thread statuses
                continue; // Continue to try dequeuing
            } else {
                sem_post(thread_statuses_lock); // Unlock the thread statuses
                break; // Exit the loop and terminate the thread
            }
        }

        thread_statuses[workerID] = 1; // Set the thread status to 'working'
        sem_post(thread_statuses_lock); // Unlock the thread statuses

        printf("[%d] DIR %s\n", workerID, curr_task); // Print the directory being worked on
        DIR *curr_dir = opendir(curr_task); // Open the directory
        while(1) {
            struct dirent *entry = readdir(curr_dir); // Read a directory entry
            if (entry == NULL) break; // Break if no more entries

            char *relative_path = malloc(sizeof(char) * 300); // Allocate memory for the relative path
            relative_path[0] = '\0'; // Initialize the relative path
            char *absolute_buffer = malloc(sizeof(char) * 300); // Allocate memory for the absolute path buffer
            absolute_buffer[0] = '\0'; // Initialize the absolute path buffer
            strcat(relative_path, curr_task); // Append the current task to the relative path
            strcat(relative_path, "/"); // Append a slash to the relative path
            strcat(relative_path, entry->d_name); // Append the directory name to the relative path

            char *absolute_path = realpath(relative_path, absolute_buffer); // Resolve the absolute path
            free(relative_path); // Free the relative path

            if (entry->d_type == DT_DIR) { // If the entry is a directory
                if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) { // If it's not '.' or '..'
                    printf("[%d] ENQUEUE %s\n", workerID, absolute_path); // Print the directory being enqueued
                    task_queue_enqueue(queue, absolute_path); // Enqueue the directory
                } else {
                    free(absolute_path); // Free the absolute path
                }
            } else if (entry->d_type == DT_REG) { // If the entry is a regular file
                char *grep_command = malloc(sizeof(char) * 50 + strlen(search_string) + strlen(absolute_path)); // Allocate memory for the grep command
                sprintf(grep_command, "grep \"%s\" \"%s\" > /dev/null", search_string, absolute_path); // Format the grep command
                int grep_status = system(grep_command); // Execute the grep command
                free(grep_command); // Free the grep command

                if (grep_status == 0) { // If the grep was successful (found the string)
                    printf("[%d] PRESENT %s\n", workerID, absolute_path); // Print that the string is present
                } else {
                    printf("[%d] ABSENT %s\n", workerID, absolute_path); // Print that the string is absent
                }
                free(absolute_path); // Free the absolute path
            }
        }
        closedir(curr_dir); // Close the directory

        sem_wait(thread_statuses_lock); // Lock the thread statuses
        thread_statuses[workerID] = 0; // Set the thread status to 'not working'
        sem_post(thread_statuses_lock); // Unlock the thread statuses
    }
    free(curr_task); // Free the current task path
}

// Main function
int main(int argc, char *argv[]) {
    num_workers = -1; // to later check if num_workers is set or not
    search_string = NULL;
    char *rootpath = NULL;
    int non_flag_count = 0; // if this variable == 0, that argument will be rootpath, 1 will be the search_string

    // dispatch parameters
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
                c_option = 1; // This prints only a count of the lines that match a pattern
        } else if (strcmp(argv[i], "-h") == 0) {
                h_option = 1; // Display the matched lines, but do not display the filenames
        } else if (strcmp(argv[i], "-i") == 0) {
                i_option = 1; // Ignores, case for matching
        } else if (strcmp(argv[i], "-l") == 0) {
                l_option = 1; // Displays list of filenames containing text only.
        } else if (strcmp(argv[i], "-n") == 0) {
                n_option = 1; // Display the matched lines and their line numbers
        } else if (strcmp(argv[i], "-v") == 0) {
                v_option = 1;
        } else if (strcmp(argv[i], "-worker") == 0) {
                num_workers = atoi(argv[++i]); // Convert the number of workers from string to integer
        } else {
                if (non_flag_count == 0) {
                    rootpath = argv[i];
                    non_flag_count++;
                } else {
                    search_string = argv[i];
                }
        }
    }

    if (num_workers == -1 || search_string == NULL || rootpath == NULL) {
        fprintf(stderr, "Must have num_workers, search string and root path\n");
        exit(1);
    }


    pthread_t workers[num_workers]; // Declare an array of worker threads
    int workerids[num_workers]; // Declare an array of worker IDs
    int statuses[num_workers]; // Declare an array of statuses

    thread_statuses = statuses; // Assign the statuses array to the global variable
    thread_statuses_lock = sem_open("/statusLock", O_CREAT, 0644, 1); // Open a semaphore for the thread statuses

    queue = malloc(sizeof(struct task_queue)); // Allocate memory for the task queue
    task_queue_initialize(queue); // Initialize the task queue

    char *rootpath_buffer = malloc(sizeof(char) * 300); // Allocate memory for the root path buffer
    char *absolute_rootpath = realpath(rootpath, rootpath_buffer); // Resolve the absolute root path
    task_queue_enqueue(queue, absolute_rootpath); // Enqueue the absolute root path

    for (int i = 0; i < num_workers; i++) {
        workerids[i] = i; // Assign worker IDs
        statuses[i] = 0; // Initialize statuses
        pthread_create(&workers[i], NULL, (void *) worker_behavior, &workerids[i]); // Create worker threads
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL); // Wait for all worker threads to finish
    }

    sem_close(thread_statuses_lock); // Close the thread statuses semaphore
    sem_unlink("/statusLock"); // Unlink the thread statuses semaphore

    task_queue_free(queue); // Free the task queue
    return 0; // Return success
}
