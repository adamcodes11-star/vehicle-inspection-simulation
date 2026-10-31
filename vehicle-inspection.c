#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <time.h>

// Maximum number of cars allowed inside the institute at the same time
#define N 10

// Total number of cars to be generated
#define K 20

// Semaphore that limits the institute capacity (max N cars)
sem_t semCap;

// Global mutex to protect shared data structures and printing
sem_t MUTEX;

// Semaphore indicating number of cars waiting in mechanic queue
sem_t semCars;

// Semaphores representing limited mechanic tools
sem_t sem_Tablet;
sem_t sem_Wrench;

// Semaphore limiting number of cars inside pollution station
sem_t semPollutionQueue;

// Semaphore indicating cars waiting for payment
sem_t semPayment;

// Semaphore indicating cars waiting for pollution testers
sem_t semPollutionCars;

// Stop flags for worker threads
int mechStop;
int paymentStop;
int pollutionStop;

// Counters for finished cars and generation completion
int finishedCars = 0, carsDone = 0;

// Represents a car entity
typedef struct Car {
    int isVip;                    // Indicates whether the car is VIP
    int Carid;                    // Unique car ID
    struct timeval start_time;    // Time when car entered the current queue
} Car;

// Node used in linked-list queues
typedef struct Node {
    Car* car;                     // Pointer to car data
    struct Node* next;            // Next node in queue
} Node;

// Queue structure supporting VIP and Regular sub-queues
typedef struct Queue {
    Node* vip_head, * vip_tail;    // VIP queue pointers
    Node* reg_head, * reg_tail;    // Regular queue pointers
    int size;                     // Total number of elements in queue
} Queue;

// Queues for each station
Queue* Mechanic_queue;
Queue* pollution_queue;
Queue* payment_queue;

// Initializes an empty queue
void queue_init(Queue* q)
{
    q->vip_head = q->vip_tail = NULL;
    q->reg_head = q->reg_tail = NULL;
    q->size = 0;
}

// Prints the current system time in HH:MM:SS.mmm format
void print_Currtime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("[%02ld:%02ld:%02ld.%03ld] ",
        (tv.tv_sec / 3600) % 24,
        (tv.tv_sec / 60) % 60,
        tv.tv_sec % 60,
        tv.tv_usec / 1000);
}

// Thread function representing a car arrival
void* CarThread(void* arg)
{
    int id = *(int*)arg;      // Car ID
    int rnum;                 // Random number for VIP decision

    // Wait until institute has available capacity
    sem_wait(&semCap);

    // Decide if car is VIP (25% probability)
    rnum = rand() % 100;
    Car* new_car = (Car*)malloc(sizeof(Car));
    new_car->Carid = id;
    new_car->isVip = (rnum < 25);

    // Protected printing of arrival
    sem_wait(&MUTEX);
    print_Currtime();
    printf("[ Main #%d ] Arrived (%s)\n", id, new_car->isVip ? "VIP" : "Regular");
    sem_post(&MUTEX);

    // Record arrival time for aging logic
    gettimeofday(&new_car->start_time, NULL);

    // Create queue node for the car
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->car = new_car;
    new_node->next = NULL;

    // Insert car into mechanic queue
    sem_wait(&MUTEX);
    Mechanic_queue->size++;

    if (new_car->isVip)
    {
        if (Mechanic_queue->vip_head == NULL)
            Mechanic_queue->vip_head = Mechanic_queue->vip_tail = new_node;
        else
        {
            Mechanic_queue->vip_tail->next = new_node;
            Mechanic_queue->vip_tail = new_node;
        }
    }
    else
    {
        if (Mechanic_queue->reg_head == NULL)
            Mechanic_queue->reg_head = Mechanic_queue->reg_tail = new_node;
        else
        {
            Mechanic_queue->reg_tail->next = new_node;
            Mechanic_queue->reg_tail = new_node;
        }
    }

    sem_post(&MUTEX);

    // Signal mechanic threads that a car is waiting
    sem_post(&semCars);

    pthread_exit(NULL);
}

// Mechanic worker thread
void* Mechanic(void* arg)
{
    int mech_id = *(int*)arg;   // Mechanic ID

    int rnum;
    double work_time;
    Node* CurrentNode = NULL;

    struct timeval now;
    double waited_ms;

    // Loop until stop signal is received
    while (!mechStop)
    {
        // Wait for available car
        sem_wait(&semCars);
        if (mechStop)
            break;

        sem_wait(&MUTEX);

        // Aging logic: promote regular car to VIP if waited too long
        if (Mechanic_queue->reg_head != NULL)
        {
            gettimeofday(&now, NULL);
            waited_ms =
                (now.tv_sec - Mechanic_queue->reg_head->car->start_time.tv_sec) * 1000.0 +
                (now.tv_usec - Mechanic_queue->reg_head->car->start_time.tv_usec) / 1000.0;

            if (waited_ms >= 400.0)
            {
                Node* promoted = Mechanic_queue->reg_head;
                Mechanic_queue->reg_head = promoted->next;
                if (Mechanic_queue->reg_head == NULL)
                    Mechanic_queue->reg_tail = NULL;

                promoted->next = NULL;

                if (Mechanic_queue->vip_head == NULL)
                    Mechanic_queue->vip_head = Mechanic_queue->vip_tail = promoted;
                else
                {
                    Mechanic_queue->vip_tail->next = promoted;
                    Mechanic_queue->vip_tail = promoted;
                }

                print_Currtime();
                printf("[ System #%d ] Aging Alert! Waited %.2fms -> promoted to VIP \n",
                    promoted->car->Carid, waited_ms);
            }
        }

        // Select next car: VIP first, then Regular
        if (Mechanic_queue->vip_head != NULL)
        {
            CurrentNode = Mechanic_queue->vip_head;
            Mechanic_queue->vip_head = CurrentNode->next;
            if (Mechanic_queue->vip_head == NULL)
                Mechanic_queue->vip_tail = NULL;
        }
        else if (Mechanic_queue->reg_head != NULL)
        {
            CurrentNode = Mechanic_queue->reg_head;
            Mechanic_queue->reg_head = CurrentNode->next;
            if (Mechanic_queue->reg_head == NULL)
                Mechanic_queue->reg_tail = NULL;
        }

        CurrentNode->next = NULL;
        Mechanic_queue->size--;

        sem_post(&MUTEX);

        // Acquire required tools
        sem_wait(&sem_Tablet);
        sem_wait(&sem_Wrench);

        // Print work start
        sem_wait(&MUTEX);
        print_Currtime();
        printf("[ Mech #%d ] Mech-%d: Working (Tools Acquired)\n",
            CurrentNode->car->Carid, mech_id);
        sem_post(&MUTEX);

        // Simulate mechanic work time
        rnum = rand() % 100;
        work_time = 0.1 + (rnum / 100.0) * 0.1;
        usleep(work_time * 1000000);

        // Release tools
        sem_post(&sem_Tablet);
        sem_post(&sem_Wrench);

        // Print completion
        sem_wait(&MUTEX);
        print_Currtime();
        printf("[ Mech #%d ] Mech-%d: Finished & Returned Tools\n",
            CurrentNode->car->Carid, mech_id);
        sem_post(&MUTEX);

        // Wait for available pollution station
        sem_wait(&semPollutionQueue);
        sem_wait(&MUTEX);

        // Reset time for pollution queue aging
        gettimeofday(&CurrentNode->car->start_time, NULL);

        // Insert into pollution queue
        if (CurrentNode->car->isVip)
        {
            if (pollution_queue->vip_head == NULL)
                pollution_queue->vip_head = pollution_queue->vip_tail = CurrentNode;
            else
            {
                pollution_queue->vip_tail->next = CurrentNode;
                pollution_queue->vip_tail = CurrentNode;
            }
        }
        else
        {
            if (pollution_queue->reg_head == NULL)
                pollution_queue->reg_head = pollution_queue->reg_tail = CurrentNode;
            else
            {
                pollution_queue->reg_tail->next = CurrentNode;
                pollution_queue->reg_tail = CurrentNode;
            }
        }

        pollution_queue->size++;

        sem_post(&MUTEX);
        sem_post(&semPollutionCars);

        // Print transfer message
        sem_wait(&MUTEX);
        print_Currtime();
        printf("[ Mech #%d ] Mech-%d: Moved Car to Pollution Queue\n",
            CurrentNode->car->Carid, mech_id);
        sem_post(&MUTEX);
    }

    pthread_exit(NULL);
}


// Pollution tester thread
void* pollution(void* arg)
{
    int id = *(int*)arg;          // Tester ID

    Node* CurrentNode = NULL;     // Pointer to current car node

    struct timeval now;           // Current time
    double waited_ms;             // Waiting time in milliseconds

    // Loop until pollution stop signal is received
    while (!pollutionStop)
    {
        // Wait for a car to be available for pollution testing
        sem_wait(&semPollutionCars);
        if (pollutionStop)
            break;

        sem_wait(&MUTEX);

        // Aging logic in pollution queue
        // Promote regular car to VIP if waiting too long
        if (pollution_queue->reg_head != NULL)
        {
            gettimeofday(&now, NULL);
            waited_ms =
                (now.tv_sec - pollution_queue->reg_head->car->start_time.tv_sec) * 1000.0 +
                (now.tv_usec - pollution_queue->reg_head->car->start_time.tv_usec) / 1000.0;

            if (waited_ms >= 400.0)
            {
                Node* promoted = pollution_queue->reg_head;
                pollution_queue->reg_head = promoted->next;
                if (pollution_queue->reg_head == NULL)
                    pollution_queue->reg_tail = NULL;

                promoted->next = NULL;

                if (pollution_queue->vip_head == NULL)
                    pollution_queue->vip_head = pollution_queue->vip_tail = promoted;
                else
                {
                    pollution_queue->vip_tail->next = promoted;
                    pollution_queue->vip_tail = promoted;
                }

                print_Currtime();
                printf("[ System #%d ] Aging Alert! waited %.2fms -> promoted to VIP \n",
                    promoted->car->Carid, waited_ms);
            }
        }

        // Select next car: VIP first, then Regular
        if (pollution_queue->vip_head != NULL)
        {
            CurrentNode = pollution_queue->vip_head;
            pollution_queue->vip_head = CurrentNode->next;
            if (pollution_queue->vip_head == NULL)
                pollution_queue->vip_tail = NULL;
        }
        else if (pollution_queue->reg_head != NULL)
        {
            CurrentNode = pollution_queue->reg_head;
            pollution_queue->reg_head = CurrentNode->next;
            if (pollution_queue->reg_head == NULL)
                pollution_queue->reg_tail = NULL;
        }

        pollution_queue->size--;
        CurrentNode->next = NULL;

        sem_post(&MUTEX);

        // Start pollution test
        sem_wait(&MUTEX);
        print_Currtime();
        printf("[ Tester #%d ] Tester-%d: Starting Pollution Test\n",
            CurrentNode->car->Carid, id);
        sem_post(&MUTEX);

        // Simulate pollution test duration
        usleep(150000);

        // Finish pollution test
        sem_wait(&MUTEX);
        print_Currtime();
        printf("[ Tester #%d ] Tester-%d: Done. Left institute.\n",
            CurrentNode->car->Carid, id);
        sem_post(&MUTEX);

        // Release pollution station capacity
        sem_post(&semPollutionQueue);

        // Move car to payment queue
        sem_wait(&MUTEX);

        payment_queue->size++;

        if (payment_queue->reg_head == NULL)
            payment_queue->reg_head = payment_queue->reg_tail = CurrentNode;
        else
        {
            payment_queue->reg_tail->next = CurrentNode;
            payment_queue->reg_tail = CurrentNode;
        }

        sem_post(&MUTEX);

        // Signal payment thread that a car is waiting
        sem_post(&semPayment);
    }

    pthread_exit(NULL);
}

// Payment processing thread
void* payment(void* arg)
{
    Node* CurrentNode = NULL;     // Current car node to process payment

    // Loop until payment stop signal is received
    while (!paymentStop)
    {
        // Wait for a car in payment queue
        sem_wait(&semPayment);
        if (paymentStop)
            break;

        sem_wait(&MUTEX);

        // Remove car from payment queue
        if (payment_queue->reg_head != NULL)
        {
            CurrentNode = payment_queue->reg_head;
            payment_queue->reg_head = payment_queue->reg_head->next;
            if (payment_queue->reg_head == NULL)
                payment_queue->reg_tail = NULL;
        }

        payment_queue->size--;

        // Free car memory after payment
        free(CurrentNode->car);
        free(CurrentNode);
        finishedCars++;

        sem_post(&MUTEX);

        // Release institute capacity slot
        sem_post(&semCap);

        sem_wait(&MUTEX);

        // Check termination condition:
        // All cars generated and all cars finished processing
        if (carsDone && finishedCars == K)
        {
            mechStop = 1;
            pollutionStop = 1;
            paymentStop = 1;

            // Wake up mechanic threads
            for (int i = 0; i < 3; i++)
                sem_post(&semCars);

            // Wake up pollution threads
            for (int i = 0; i < 2; i++)
                sem_post(&semPollutionCars);

            // Wake up payment thread
            sem_post(&semPayment);
        }

        sem_post(&MUTEX);
    }

    pthread_exit(NULL);
}

// Main function: initializes system and starts all threads
int main(int argc, char* argv[])
{
    int id[K];                   // Car IDs
    int MechanicID[3];           // Mechanic IDs
    int TesterID[2];             // Pollution tester IDs

    pthread_t thread[K];         // Car threads
    pthread_t Mechanic_threads[3];
    pthread_t Pollution_threads[2];
    pthread_t payment_thread;
    printf("--- Institute Open (N=10, K=20) ---\n");
    // Initialize random seed
    srand(time(NULL));

    // Allocate queues
    Mechanic_queue = malloc(sizeof(Queue));
    pollution_queue = malloc(sizeof(Queue));
    payment_queue = malloc(sizeof(Queue));

    // Initialize queues
    queue_init(Mechanic_queue);
    queue_init(pollution_queue);
    queue_init(payment_queue);

    // Initialize stop flags
    mechStop = pollutionStop = paymentStop = 0;

    // Initialize semaphores
    sem_init(&semCap, 0, N);
    sem_init(&semCars, 0, 0);
    sem_init(&sem_Tablet, 0, 2);
    sem_init(&sem_Wrench, 0, 2);
    sem_init(&MUTEX, 0, 1);
    sem_init(&semPollutionQueue, 0, 3);
    sem_init(&semPollutionCars, 0, 0);
    sem_init(&semPayment, 0, 0);

    // Create mechanic threads
    for (int i = 0; i < 3; i++)
    {
        MechanicID[i] = i + 1;
        pthread_create(&Mechanic_threads[i], NULL, Mechanic, &MechanicID[i]);
    }

    // Create pollution tester threads
    for (int i = 0; i < 2; i++)
    {
        TesterID[i] = i + 1;
        pthread_create(&Pollution_threads[i], NULL, pollution, &TesterID[i]);
    }

    // Create payment thread
    int payment_id = 1;
    pthread_create(&payment_thread, NULL, payment, &payment_id);

    // Generate car threads with delay
    for (int i = 0; i < K; i++)
    {
        usleep(50000);
        id[i] = i + 1;
        pthread_create(&thread[i], NULL, CarThread, &id[i]);
    }

    // Wait for all car threads to finish
    for (int i = 0; i < K; i++)
        pthread_join(thread[i], NULL);

    // Indicate that no more cars will arrive
    printf("All cars generated. Waiting for completion... \n");
    carsDone = 1;

    // Wait for worker threads
    for (int i = 0; i < 3; i++)
        pthread_join(Mechanic_threads[i], NULL);

    for (int i = 0; i < 2; i++)
        pthread_join(Pollution_threads[i], NULL);

    pthread_join(payment_thread, NULL);

    // Free allocated memory
    free(Mechanic_queue);
    free(pollution_queue);
    free(payment_queue);
    return 0;
}