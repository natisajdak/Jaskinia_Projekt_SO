#ifndef IPC_H
#define IPC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <pthread.h>
#include "config.h"

// Maksymalna tablica 
#define MAX_ZWIEDZAJACYCH_TABLICA 2000

// === PAMIĘĆ DZIELONA ===
typedef struct {
    // Liczniki osób na trasach
    int trasa1_licznik;
    int trasa2_licznik;
    
    // Kładki
    int kladka1_licznik;
    int kladka1_kierunek;       // 0=wejście, 1=wyjście
    int kladka2_licznik;
    int kladka2_kierunek;
    
    
    int bilety_powrot;
    int bilety_dzieci_bez_opieki;

    // Statystyki
    int bilety_sprzedane;
    int bilety_trasa1;
    int bilety_trasa2;
    
    // Licznik wszystkich wygenerowanych zwiedzających
    int licznik_wygenerowanych;  // Inkrementowany przez generator
    int licznik_odrzuconych;     // Odmowy biletu
    
    // Grupy aktywne
    int grupa1_aktywna;
    int grupa2_aktywna;
    
    // PID procesów
    pid_t pid_main;
    pid_t pid_kasjer;
    pid_t pid_przewodnik1;
    pid_t pid_przewodnik2;
    pid_t pid_straznik;
    pid_t pid_generator; 
    
    // Flagi zamknięcia
    volatile sig_atomic_t zamkniecie_przewodnik1;
    volatile sig_atomic_t zamkniecie_przewodnik2;
    
    int jaskinia_otwarta;
    
    // Tablica aktywnych zwiedzających (dynamiczna lista)
    pid_t zwiedzajacy_pids[MAX_ZWIEDZAJACYCH_TABLICA];
    int liczba_aktywnych;
    int zwiedzajacy_jest_opiekunem[MAX_ZWIEDZAJACYCH_TABLICA]; 
    
    // Czas startu
    time_t czas_startu;
    time_t czas_rzeczywisty_start;
    int symulowany_czas_sekund;

    int trasa1_wycieczka_nr;
    int trasa2_wycieczka_nr;
    int trasa1_czeka_na_grupe;
    int trasa2_czeka_na_grupe;

    // === KOLEJKI OCZEKUJĄCYCH ===
    pid_t kolejka_trasa1[MAX_ZWIEDZAJACYCH_TABLICA];
    int kolejka_trasa1_poczatek;
    int kolejka_trasa1_koniec;
    
    pid_t kolejka_trasa2[MAX_ZWIEDZAJACYCH_TABLICA];
    int kolejka_trasa2_poczatek;
    int kolejka_trasa2_koniec;
    
    // Aktualne grupy w trasie
    pid_t grupa1_pids[N1];
    int grupa1_liczba;
    int grupa1_czeka_na_wpuszczenie; 
    
    pid_t grupa2_pids[N2];
    int grupa2_liczba;
    int grupa2_czeka_na_wpuszczenie;

    
} StanJaskini;


// KOLEJKA KOMUNIKATÓW (Kasjer ↔ Zwiedzający)

typedef struct {
    long mtype; 
    
    // Dane zwiedzającego (request)
    int zwiedzajacy_pid;
    int wiek;
    int trasa;
    int czy_powrot;             
    
    int jest_opiekunem;           // 1 = ten proces ma wątek-dziecko
    int wiek_dziecka;             
    
    // Odpowiedź kasjera (response)
    int bilet_wydany;
    float cena;
    char powod_odmowy[128];
    
    time_t timestamp;
} MsgBilet;

// UNIA DLA SEMCTL (wymagane na niektórych systemach)

union semun {
    int val;                    // Wartość dla SETVAL
    struct semid_ds *buf;       // Buffer dla IPC_STAT, IPC_SET
    unsigned short *array;      // Tablica dla GETALL, SETALL
};

// === FUNKCJE POMOCNICZE IPC ===

// Pamięć dzielona
int utworz_pamiec_dzielona(void);
StanJaskini* podlacz_pamiec_dzielona(int shmid);
void odlacz_pamiec_dzielona(StanJaskini *stan);
void usun_pamiec_dzielona(int shmid);

// Semafory
int utworz_semafory(void);
void inicjalizuj_semafory(int semid);
void sem_wait_safe(int semid, int sem_num);
void sem_signal_safe(int semid, int sem_num);
int sem_getval_safe(int semid, int sem_num);
void usun_semafory(int semid);  

// Kolejka komunikatów
int utworz_kolejke(void);
void usun_kolejke(int msgid);

void zarejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid);
void wyrejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid);

// === Funkcje kolejkowania ===
// Zwiedzający dołącza do kolejki oczekujących na trasę
void dolacz_do_kolejki(int trasa, pid_t pid, StanJaskini *stan, int semid);

// Przewodnik zbiera grupę z kolejki (zwraca liczbę zebranych osób)
int zbierz_grupe(int nr_trasy, StanJaskini *stan, int semid, int max_osob);
int dolacz_pare_do_kolejki(int trasa, pid_t pid_dziecko, pid_t pid_opiekun, 
                           StanJaskini *stan, int semid);
// Funkcje pomocnicze
void wypisz_stan_jaskini(StanJaskini *stan);
void zapisz_log_symulacji(const char *format, ...);

#endif // IPC_H