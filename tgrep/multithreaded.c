#include <stdio.h>    // Standard I/O functions
#include <stdlib.h>   // Standard library for memory allocation, process control, conversions, etc.
#include <sys/types.h> // System data types
#include <unistd.h>   // POSIX operating system API
#include <dirent.h>   // Directory entry
#include <pthread.h>  // POSIX threads
#include <semaphore.h> // POSIX semaphores
#include <fcntl.h>    // File control options
#include <string.h>   // String operations

//declaring for options
int opt_count_only = 0, opt_no_filenames = 0, opt_ignore_case = 0, opt_list_filenames = 0, opt_line_numbers = 0, opt_invert_match = 0;

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
    int workerID = *id;
    char *curr_task = malloc(sizeof(char) * 300);

    while (1) {
        sem_wait(thread_statuses_lock);
        int status = task_queue_dequeue(queue, curr_task);
        if (status < 0) {
            if (all_done_checker() < 0) {
                sem_post(thread_statuses_lock);
                continue;
            } else {
                sem_post(thread_statuses_lock);
                break;
            }
        }

        thread_statuses[workerID] = 1;
        sem_post(thread_statuses_lock);

        DIR *curr_dir = opendir(curr_task);
        if (curr_dir == NULL) {
            fprintf(stderr, "Failed to open directory: %s\n", curr_task);
            printf("Try Agian...\n\n");
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(curr_dir)) != NULL) {
            if (entry->d_type == DT_REG) {  // Only consider regular files for grep
                char *relative_path = malloc(sizeof(char) * 300);
                sprintf(relative_path, "%s/%s", curr_task, entry->d_name);
                
                char *absolute_path = realpath(relative_path, NULL);
                free(relative_path);
                
                if (absolute_path) {
                    char grep_command[1024];
                    sprintf(grep_command, "grep -s -a"); // Use -s to suppress error messages
                    if (opt_ignore_case) strcat(grep_command, " -i");
                    if (opt_count_only) strcat(grep_command, " -c");
                    if (opt_no_filenames) strcat(grep_command, " -h");
                    if (opt_line_numbers) strcat(grep_command, " -n");
                    if (opt_invert_match) strcat(grep_command, " -v");
                    if (opt_list_filenames) strcat(grep_command, " -l");
                    sprintf(grep_command + strlen(grep_command), " \"%s\" \"%s\"", search_string, absolute_path);

                    printf("Executing command: %s\n", grep_command);  // Debug print

                    FILE *fp = popen(grep_command, "r");
                    char result[1024];
                    if (fgets(result, sizeof(result), fp) == NULL) {
    if (feof(fp)) {
        printf("No output from grep command.\n");
    } else {
        printf("Error reading from pipe.\n");
    }
} else {
    do {
        printf("result: %s\n", result);  // Debug result
        if (opt_list_filenames) {
            printf("%s\n", absolute_path);
            break;
        } else {
            printf("[%d] %s: %s", workerID, absolute_path, result);
        }
    } while (fgets(result, sizeof(result), fp) != NULL);
}
int status = pclose(fp);
if (status != 0) {
    if (WIFEXITED(status)) {
        int exit_status = WEXITSTATUS(status);
        printf("grep exited with status %d\n", exit_status);
    } else {
        printf("grep did not exit normally\n");
    }
}

                    free(absolute_path);
                }
            }
        }
        closedir(curr_dir);

        sem_wait(thread_statuses_lock);
        thread_statuses[workerID] = 0;
        sem_post(thread_statuses_lock);
    }
    free(curr_task);
}

void print_help() {
    printf("Usage: ./tgrep num_workers root_path \"search_string\" [options]\n");
    printf("Options:\n");
    printf("  -c      Print only a count of the lines that match a pattern.\n");
    printf("  -h      Display the matched lines, but do not display the filenames.\n");
    printf("  -i      Ignore case distinctions in both the pattern and the input files.\n");
    printf("  -l      Display list of filenames only.\n");
    printf("  -n      Display the matched lines and their line numbers.\n");
    printf("  -v      Invert the sense of matching, to select non-matching lines.\n");
    printf("  -help   Display this help and exit.\n");
}

// Main function
int main(int argc, char *argv[]) {

    if (argc == 2 && strcmp(argv[1], "-help") == 0) {
        print_help();
        return 0;
    }

    if (argc < 5) { // Ensures there's at least one option
        fprintf(stderr, "Usage: %s num_workers root_path \"search_string\" [-options]\n", argv[0]);
        return 1;
    }

    // Positional arguments are expected to be the first three arguments after the program name
    num_workers = atoi(argv[1]);
    char *rootpath = argv[2];
    search_string = argv[3];

    // Initialize option flags
    opt_count_only = 0;
    opt_no_filenames = 0;
    opt_ignore_case = 0;
    opt_list_filenames = 0;
    opt_line_numbers = 0;
    opt_invert_match = 0;

    // Parse options provided after the required positional arguments
    for (int i = 4; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] == '-') { // Check if the argument starts with a hyphen
            switch (arg[1]) {
                case 'c':
                    opt_count_only = 1;
                    break;
                case 'h':
                    opt_no_filenames = 1;
                    break;
                case 'i':
                    opt_ignore_case = 1;
                    break;
                case 'l':
                    opt_list_filenames = 1;
                    break;
                case 'n':
                    opt_line_numbers = 1;
                    break;
                case 'v':
                    opt_invert_match = 1;
                    break;
                default:
                    fprintf(stderr, "Unknown option: %s\n", arg);
                    return 1;
            }
        }
    }

    printf("Number of workers: %d\n", num_workers);
    printf("Root path: %s\n", rootpath);
    printf("Search string: '%s'\n", search_string);

    pthread_t workers[num_workers]; // Declare an array of worker threads
    int workerids[num_workers];     // Declare an array of worker IDs
    int statuses[num_workers];      // Declare an array of statuses
    thread_statuses = statuses;     // Assign the statuses array to the global variable
    thread_statuses_lock = sem_open("/statusLock", O_CREAT, 0644, 1); // Open a semaphore for the thread statuses

    queue = malloc(sizeof(struct task_queue)); // Allocate memory for the task queue
    task_queue_initialize(queue);              // Initialize the task queue

    char *rootpath_buffer = malloc(sizeof(char) * 300);             // Allocate memory for the root path buffer
    char *absolute_rootpath = realpath(rootpath, rootpath_buffer);  // Resolve the absolute root path
    task_queue_enqueue(queue, absolute_rootpath);                   // Enqueue the absolute root path

    for (int i = 0; i < num_workers; i++) {
        workerids[i] = i;                    // Assign worker IDs
        statuses[i] = 0;                     // Initialize statuses
        pthread_create(&workers[i], NULL, (void *)worker_behavior, &workerids[i]); // Create worker threads
    }

    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);  // Wait for all worker threads to finish
    }

    sem_close(thread_statuses_lock);  // Close the thread statuses semaphore
    sem_unlink("/statusLock");        // Unlink the thread statuses semaphore

    task_queue_free(queue);           // Free the task queue
    free(rootpath_buffer);            // Free allocated buffer

    return 0; // Return success
}
