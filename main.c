#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>
#include <semaphore.h>
#include <unistd.h> // swap w/ universal header for windows?
#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "helper_structs.h"

#define INTRPTED 1
#define SUCCESS 0
#define NANO_TO_SEC 1000000000L

// threads
void* handle_pc(void*);
void* transmit(void*);
void* handle_brojac(void*);
void* control_pcs(void*);

int is_running(void);

int get_lag_time(int);
int get_tm_wait(void);

void queue_col(short);
void remove_col(short);

int send_tm(short);
void end_tm(short);
struct timespec get_next_tm(void);

int pc_ids[PC_CNT * sizeof(int)]; // better way to do this?
struct racunar_t pcs[PC_CNT];
pthread_t pc_threads[PC_CNT];
short collisions[PC_CNT] = {0};

struct magistrala_t mag;
uint64_t start_time;
uint64_t curr_time;

pthread_mutex_t tm_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t tm_intrpt_cond = PTHREAD_COND_INITIALIZER;
sem_t sem;

int ts_stopped = 0;
int tm_total = 0; // total successful transmissions
uint64_t end_time; // time when the sim should end (start + SIM_LEN)

int total_intrpts = 0;
int total_colls = 0;

int get_lag_time(int interrupt_cnt) {
    int lag;

    int p = (interrupt_cnt > MAX_INTR) ? MAX_INTR : interrupt_cnt;
    p = pow(2, p);

    lag = (rand() % p) * 2; // get ms

    return lag;
}

void print_stats_per_s() {
    printf("Sec: %d\n", (int)((curr_time - start_time) / MICRO_TO_S));
    printf("Uspesnih transmisija u prosloj sekundi: %d\n", mag.brojac);
    printf("Ukupno uspesnih transmisija: %d\n", tm_total);
    printf("Ukupno prekida: %d\n", total_intrpts);
    printf("-------------------------\n");
}
void final_print() {
    printf("Simulacija zavrsena!\n");
    printf("Procenat iskoriscenja mreze: %f%%\n", ((float)tm_total / (float)MAX_MAG_USAGE) * 100); // get percentage
    printf("Ukupno uspesnih transmisija: %d\n", tm_total);
    printf("Ukupno prekida: %d\n", total_intrpts);
    printf("Na %d kolizija\n", total_colls);
    printf("-------------------------\n");
}

void* handle_brojac(void* atr) { // Handle timers
    struct timeval tv;

    mag.brojac = 0;

    curr_time = start_time;
    long last_time = start_time;

    int new_sec = 0;

    while (is_running()) {

        new_sec += (curr_time - last_time);

        // New second
        if (new_sec >= MICRO_TO_S) {
            print_stats_per_s();

            tm_total += mag.brojac;

            new_sec = 0;
            mag.brojac = 0;
        }

        // Update new time
        gettimeofday(&tv, NULL);
        last_time = curr_time;
        curr_time = tv.tv_sec * MICRO_TO_S + tv.tv_usec;

        usleep(1000); // 1000 microsecs - 1 ms
    }

    // Last part of last sec
    tm_total += mag.brojac;
    mag.brojac = 0;

    return NULL;
}

int is_running() { // is simulation running?
    if (curr_time < end_time)
        return 1;
    return 0;
}

void queue_col(short id) {
    collisions[id] = 1;
}
void remove_col(short id) {
    collisions[id] = 0;
}

struct timespec get_next_tm() {
    struct timespec ts;
    long tm_len = TM_LEN * 1000L; // micro to nano

    // start time
    ts.tv_sec = mag.pt / MICRO_TO_S;
    ts.tv_nsec = (mag.pt % MICRO_TO_S) * 1000L;

    // target time
    ts.tv_nsec += tm_len;

    while (ts.tv_nsec >= NANO_TO_SEC) {
        ts.tv_sec++;
        ts.tv_nsec -= NANO_TO_SEC;
    }
    return ts;
}

void* transmit(void* atr) {
    struct timespec ts;

    while(is_running())
        if (mag.racunar_id == FREE_MAG)
            usleep(1000);
        else
        {
            // conditional mutex (timer or interrupt)
            pthread_mutex_lock(&tm_lock);
            ts = get_next_tm();
            while (!ts_stopped) {
                int res = pthread_cond_timedwait(&tm_intrpt_cond, &tm_lock, &ts);
                if (res == ETIMEDOUT) { // Not interrupted, tm ended naturally
                    end_tm(!INTRPTED);
                    break;
                }
            }
            ts_stopped = 0;
            pthread_mutex_unlock(&tm_lock);
        }
    return NULL;
}

int send_tm(short id) { // returns 1 if successful or col in 2ms
    sem_wait(&sem);

    if (mag.racunar_id == FREE_MAG) {
        mag.racunar_id = id;
        mag.pt = curr_time;
        sem_post(&sem);
        return 1;
    }
    if (curr_time - mag.pt < 2000) { // Collision
        sem_post(&sem);
        return 1;
    }

    sem_post(&sem);
    return 0;
}

void end_tm(short intrpt) {

    sem_wait(&sem);

    if (mag.racunar_id == FREE_MAG) {
        sem_post(&sem);
        return;
    }

    if (intrpt) { // interrupted transmission
        total_intrpts++;
        for (int i = 0; i < PC_CNT; i++) {
            if (collisions[i]) {
                total_colls++;
            }
        }
    } else // successful transmission
        mag.brojac++;

    mag.racunar_id = FREE_MAG;

    sem_post(&sem);
}

int get_tm_wait() { // microsecs
    return (rand() % (MAX_WAIT - MIN_WAIT)) + MIN_WAIT + 1;
}

void* control_pcs(void* atr) {
    short occupied_cnt;

    while(is_running()) {

        occupied_cnt = 0;
        pthread_mutex_lock(&tm_lock);

        for (int i = 0; i < PC_CNT; i++)
            if (pcs[i].stanje == 0) {
                occupied_cnt++;
            }
        if (occupied_cnt > 1) {
            for (int i = 0; i < PC_CNT; i++) {
              if (pcs[i].stanje == 0)
                  collisions[i] = 1;
            }

            // stop ts
            ts_stopped = 1;
            end_tm(INTRPTED);
            pthread_cond_signal(&tm_intrpt_cond);
        }
        pthread_mutex_unlock(&tm_lock);
        usleep(1000);
    }
    return NULL;
}

void* handle_pc(void* atr) {
    short id = *(int*) atr;

    usleep(get_tm_wait());
    while (is_running()) {

        // ready for next tm
        if (pcs[id].stanje == 1) {
            if (send_tm(id)) {
                pcs[id].stanje = 0;

                usleep(TM_LEN);

                // Successful tm
                if (!collisions[id]) {
                    pcs[id].k = 0;
                    pcs[id].stanje = 1;
                    usleep(get_tm_wait() - TM_LEN);
                } else {
                    pcs[id].stanje = 2;
                }
            } else {
                usleep(1000);
            }
        // Collision
        } else {
            remove_col(id);
            pcs[id].k++;
            pcs[id].stanje = 2;
            long lag_time = get_lag_time(pcs[id].k) * 1000 - TM_LEN;
            if (lag_time < 0) lag_time = 0;
            //printf("%li\n", lag_time);
            usleep(lag_time);
            pcs[id].stanje = 1;
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    printf("Started!\n");
    printf("-------------------------\n");

    // Init stanja
    for (int i = 0; i < PC_CNT; i++) {
        pcs[i].stanje = 1;
        pcs[i].k = 0;
    }

    // Init time
    struct timeval tv;
    gettimeofday(&tv, NULL);
    end_time = (tv.tv_sec + SIM_LEN) * MICRO_TO_S + tv.tv_usec;
    start_time = tv.tv_sec * MICRO_TO_S + tv.tv_usec;

    // Default to no transmission
    mag.racunar_id = FREE_MAG;

    // Set random seed
    srand(tv.tv_sec); // up for change in <time.h>

    // init semaphore
    sem_init(&sem, 0, 1);

    // Start time thread
    pthread_t timet;
    pthread_create(&timet, NULL, handle_brojac, NULL);

    // Start control thread
    pthread_t controlt;
    pthread_create(&controlt, NULL, control_pcs, NULL);

    // Start transmit thread
    pthread_t transmit_thread;
    pthread_create(&transmit_thread, NULL, transmit, NULL);

    // Start PC threads
    for (int i = 0; i < PC_CNT; i++) {
        pc_ids[i] = i;
        pthread_create(&pc_threads[i], NULL, handle_pc, &pc_ids[i]);
    }

    for (int i = 0; i < PC_CNT; i++)
        pthread_join(pc_threads[i], NULL);
    printf("Joined pcs\n");

    pthread_join(timet, NULL);
    printf("Joined time\n");
    pthread_join(controlt, NULL);
    printf("Joined control\n");
    pthread_join(transmit_thread, NULL);
    printf("Joined transmit\n");

    pthread_mutex_destroy(&tm_lock);
    pthread_cond_destroy(&tm_intrpt_cond);

    sem_destroy(&sem);

    final_print();

    exit(0);
}
