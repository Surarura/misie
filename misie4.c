#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#define TAG_REQUEST 1
#define TAG_ACK 2
#define TAG_RELEASE 3

#define MAX_QUEUE 100
#define MAX_Z 5
#define MIN_Z 1
#define SLEEP_TIME 1 // maks czas na odpoczynek lub pracę
#define RESET_COLOR "\033[0m"
#define DEBUG 1

typedef enum { IDLE, WAITING, IN_CS } State;

typedef struct {
    int timestamp;
    int id;
    int z;
} Request;

const char* colors[] = {
    "\033[31m", // red
    "\033[32m", // green
    "\033[33m", // yellow
    "\033[34m", // blue
    "\033[35m", // magenta 
    "\033[37m", // white
    "\033[91m", // bright red
    "\033[92m", // bright green
    "\033[93m", // bright yellow
    "\033[94m", // bright blue
    "\033[95m", // bright magenta
    "\033[96m", // bright cyan
};

//zmienne globalne żeby funkcje je mogły uzywac
int K = 2;  // liczba doków
int M = 10; // liczba mechaników

int id, size;
int lamport_clock = 0;

Request queue[MAX_QUEUE];
int queue_size = 0;

int ack_count = 0;
State state = IDLE;
int my_z = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ack_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clock_mutex = PTHREAD_MUTEX_INITIALIZER;

void increment_clock() {
    pthread_mutex_lock(&clock_mutex);
    lamport_clock++;
    pthread_mutex_unlock(&clock_mutex);
}

void update_clock(int received) {
    pthread_mutex_lock(&clock_mutex);
    if (received > lamport_clock){
        lamport_clock = received;
        
    }
    lamport_clock++;
    pthread_mutex_unlock(&clock_mutex);
   
}

// Dodaje żądanie do kolejki
void add_to_queue(Request req) {
    pthread_mutex_lock(&queue_mutex);
    queue[queue_size++] = req;
    pthread_mutex_unlock(&queue_mutex);
}

// Usuwa żądanie z kolejki
void remove_from_queue(int id) {
   pthread_mutex_lock(&queue_mutex);
    for (int i = 0; i < queue_size; i++) {
        if (queue[i].id == id) {
            for (int j = i; j < queue_size - 1; j++)
                queue[j] = queue[j + 1];
            queue_size--;
            break;
        }
    }
    pthread_mutex_unlock(&queue_mutex);
}

// Porównywanie do sortowania
int compare_requests(const void *a, const void *b) {
    Request *r1 = (Request *)a;
    Request *r2 = (Request *)b;
    if (r1->timestamp != r2->timestamp)
        return r1->timestamp - r2->timestamp;
    return r1->id - r2->id;
}

// Sortowanie kolejki
void sort_queue() {
    pthread_mutex_lock(&queue_mutex);
    qsort(queue, queue_size, sizeof(Request), compare_requests);
    pthread_mutex_unlock(&queue_mutex);
}

// Suma Z innych procesów przed moim
int sum_z_above_me(int my_id) {
    pthread_mutex_lock(&queue_mutex);
    int sum = 0;
    for (int i = 0; i < queue_size; i++) {
        if (queue[i].id == my_id)
            break;
        sum += queue[i].z;
    }
    pthread_mutex_unlock(&queue_mutex);
    return sum;
}

void send_request(int timestamp, int id, int z) {
    Request req = { timestamp, id, z };
    for (int i = 0; i < size; i++) {
        if (i != id)
            MPI_Send(&req, sizeof(Request) / sizeof(int), MPI_INT, i, TAG_REQUEST, MPI_COMM_WORLD);
    }
}

void send_release(int id) {
    Request dummy = { lamport_clock, id, 0 };
    for (int i = 0; i < size; i++) {
        if (i != id)
            MPI_Send(&dummy, sizeof(Request) / sizeof(int), MPI_INT, i, TAG_RELEASE, MPI_COMM_WORLD);
    }
}

void print_queue(const char* process_color) {
    printf("%sProces %d [t%d] -> Kolejka: [", process_color, id, lamport_clock);
    for (int i = 0; i < queue_size; i++) {
        // Używamy koloru procesu drukującego dla wszystkich elementów
        printf("P%d (t%d, Z=%d)", queue[i].id, queue[i].timestamp, queue[i].z);
        if (i < queue_size - 1) {
            printf(", ");
        }
    }
    printf("]%s\n", RESET_COLOR);
}

void handle_messages(const char* color) {
    MPI_Status status;
    int flag;
    Request msg;

    do {
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status); //nieblokująco sprawdza czy są wiadomości, da flag 1 jak jest wiadomość doodebrania
        if (flag) {
            MPI_Recv(&msg, sizeof(Request) / sizeof(int), MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
            
            update_clock(msg.timestamp);
            int local_lamport = lamport_clock;

            if (status.MPI_TAG == TAG_REQUEST) {
                if (DEBUG == 1)
                    printf("%sProces %d [t%d] otrzymał request od Procesu %d [t%d] %s\n", color, id, local_lamport, status.MPI_SOURCE, msg.timestamp, RESET_COLOR);
                add_to_queue(msg);
                sort_queue();
                MPI_Send(NULL, 0, MPI_INT, msg.id, TAG_ACK, MPI_COMM_WORLD);
            } else if (status.MPI_TAG == TAG_ACK) {
                if (DEBUG == 1)
                    printf("%sProces %d [t%d] otrzymał ACK od Procesu %d [t%d] %s\n", color, id, local_lamport, status.MPI_SOURCE, msg.timestamp, RESET_COLOR);
                pthread_mutex_lock(&ack_mutex);
                ack_count++;
                pthread_mutex_unlock(&ack_mutex);
            } else if (status.MPI_TAG == TAG_RELEASE) {
                if (DEBUG == 1)
                    printf("%sProces %d [t%d] otrzymał RELEASE od Procesu %d [t%d] %s\n", color, id, local_lamport, status.MPI_SOURCE, msg.timestamp, RESET_COLOR);
                remove_from_queue(msg.id);
                
            }
        }
    } while (flag); //odbierze all wiadomości
}

void* message_handler(void* arg){
     char* color = (char*) arg;
    while (1){
        handle_messages(color);
    }
    
}

int main(int argc, char **argv) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nie wspiera pełnej wielowątkowości!\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char* my_color = colors[id % (sizeof(colors) / sizeof(colors[0]))];

    pthread_t mess_thread;
    pthread_create(&mess_thread, NULL, message_handler, my_color);

    srand(time(NULL) + id * 123);

    


    while (1) {
        

        if (state == IDLE) {
            
            if ((rand() % 3) == 0) {
                // losowa potrzeba naprawy
                increment_clock();
                my_z = rand() % (MAX_Z - MIN_Z + 1) + MIN_Z;
                int temp_lamport = lamport_clock;
                Request my_req = { temp_lamport, id, my_z };
                add_to_queue(my_req);
                sort_queue();
                ack_count = 0;
                send_request(temp_lamport, id, my_z);
                state = WAITING;
                printf("%sProces %d [t%d] -> żąda naprawy (Z = %d)%s\n", my_color, id, lamport_clock, my_z, RESET_COLOR);
            } else {
                printf("%sProces %d [t%d] -> odpoczywa%s\n", my_color, id, lamport_clock, RESET_COLOR);
                sleep(SLEEP_TIME);
            }

        } else if (state == WAITING) {
            //printf("Proces %d -> Oczekuje na ACK (ACK = %d)\n", id, ack_count);
            int pos = -1;
            for (int i = 0; i < queue_size; i++) {
                if (queue[i].id == id) {
                    pos = i;
                    break;
                }
            }

            if (ack_count == size - 1 && pos >= 0 && pos < K && sum_z_above_me(id) + my_z <= M) {
                state = IN_CS;
                printf("%sProces %d [t%d] -> wchodzi do sekcji krytycznej (zajmuje Z = %d, obecne ACK = %d)%s\n", my_color, id, lamport_clock, my_z, ack_count, RESET_COLOR);
                if (DEBUG == 1)
                    print_queue(my_color);
            } //else if(ack_count == size - 1 && pos >= 0){
                //printf("Proces %d -> oczekuje na zasoby\n", id);
            //}

        } else if (state == IN_CS) {
            sleep(1 + rand() % 10); // symulacja naprawy
            printf("%sProces %d [t%d] -> wychodzi z sekcji krytycznej%s\n", my_color, id, lamport_clock, RESET_COLOR);

            increment_clock();
            send_release(id);
            remove_from_queue(id);
            state = IDLE;
        }
        usleep(100000); // 100ms
    }
    MPI_Finalize();
    return 0;
}
