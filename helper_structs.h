#ifndef helper_structs
#define helper_structs

#define PC_CNT 10
#define MIN_WAIT 50000 // micros -  50 ms
#define MAX_WAIT 150000 // micros - 150 ms
#define TM_LEN 10000 // micros - 10 ms
#define MAX_INTR 10 // max interrupts in a row
#define MICRO_TO_S 1000000L // microseconds = second
#define SIM_LEN 60 // length of simulation, 60 secs
#define FREE_MAG -1 // free magistrala
#define MAX_MAG_USAGE 6000

struct racunar_t {
    short stanje; // 0 – transmituje, 1 – čeka na novu transmisiju, 2 – čeka na retransmisiju
    short k; // broj uzastopnih kolizija
};

struct magistrala_t {
    short racunar_id; // id računara koji je počeo transmisiju
    short brojac; // brojač okvira prenetih bez prekida kroz mrežu
    uint64_t pt; // početak transmisije u mikrosekundama
};

// unused
struct racunar_atr {
    int id;

};

#endif
