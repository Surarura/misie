#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#define TAG_REQUEST 1
#define TAG_ACK 2
#define TAG_RELEASE 3

#define MAX_QUEUE 100
#define MAX_Z 5
#define MIN_Z 1
#define SLEEP_TIME 1 // maks czas na odpoczynek lub pracę

typedef enum { IDLE, WAITING, IN_CS } State;

typedef struct {
    int timestamp;
    int id;
    int z;
} Request;

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


void increment_clock() {
    lamport_clock++;
}

void update_clock(int received) {
    if (received > lamport_clock)
        lamport_clock = received;
    lamport_clock++;
}

// Dodaje żądanie do kolejki
void add_to_queue(Request req) {
    queue[queue_size++] = req;
}

// Usuwa żądanie z kolejki
void remove_from_queue(int id) {
    for (int i = 0; i < queue_size; i++) {
        if (queue[i].id == id) {
            for (int j = i; j < queue_size - 1; j++)
                queue[j] = queue[j + 1];
            queue_size--;
            break;
        }
    }
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
    qsort(queue, queue_size, sizeof(Request), compare_requests);
}

// Suma Z innych procesów przed moim
int sum_z_above_me(int my_id) {
    int sum = 0;
    for (int i = 0; i < queue_size; i++) {
        if (queue[i].id == my_id)
            break;
        sum += queue[i].z;
    }
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

void handle_messages() {
    MPI_Status status;
    int flag;
    Request msg;

    do {
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status); //nieblokująco sprawdza czy są wiadomości, da flag 1 jak jest wiadomość doodebrania
        if (flag) {
            MPI_Recv(&msg, sizeof(Request) / sizeof(int), MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, &status);
            update_clock(msg.timestamp);

            if (status.MPI_TAG == TAG_REQUEST) {
                add_to_queue(msg);
                sort_queue();
                MPI_Send(NULL, 0, MPI_INT, msg.id, TAG_ACK, MPI_COMM_WORLD);
            } else if (status.MPI_TAG == TAG_ACK) {
                ack_count++;
            } else if (status.MPI_TAG == TAG_RELEASE) {
                remove_from_queue(msg.id);
            }
        }
    } while (flag); //odbierze all wiadomości
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    srand(time(NULL) + id * 123);

    while (1) {
        handle_messages();

        if (state == IDLE) {
            handle_messages();
            increment_clock();
            if ((rand() % 3) == 0) {
                // losowa potrzeba naprawy
                my_z = rand() % (MAX_Z - MIN_Z + 1) + MIN_Z;
                Request my_req = { lamport_clock, id, my_z };
                add_to_queue(my_req);
                sort_queue();
                ack_count = 0;
                send_request(lamport_clock, id, my_z);
                state = WAITING;
                printf("Proces %d -> żąda naprawy (Z = %d)\n", id, my_z);
            } else {
                printf("Proces %d -> odpoczywa\n", id);
                sleep(SLEEP_TIME);
            }

        } else if (state == WAITING) {
            handle_messages();
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
                printf("Proces %d -> wchodzi do sekcji krytycznej (zajmuje Z = %d, obecne ACK = %d)\n", id, my_z, ack_count);
            } //else if(ack_count == size - 1 && pos >= 0){
                //printf("Proces %d -> oczekuje na zasoby\n", id);
            //}

        } else if (state == IN_CS) {
            handle_messages();
            sleep(1 + rand() % 3); // symulacja naprawy
            printf("Proces %d -> wychodzi z sekcji krytycznej\n", id);

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
