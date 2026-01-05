#ifndef IPC_H
#define IPC_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include "config.h"

// === PAMIĘĆ DZIELONA ===

typedef struct {
    // Liczniki osób na trasach
    int trasa1_licznik;
    int trasa2_licznik;
    
    // Kładki
    int kladka1_licznik;        // Ile osób obecnie na kładce 1
    int kladka2_licznik;        // Ile osób obecnie na kładce 2
    int kladka1_kierunek;       // 0=wejście, 1=wyjście
    int kladka2_kierunek;
    
    int bilety_powrot;             
    int bilety_dzieci_bez_opieki;

    // Statystyki
    int bilety_sprzedane;       // Łączna liczba sprzedanych biletów
    int bilety_trasa1;          // Bilety na trasę 1
    int bilety_trasa2;          // Bilety na trasę 2
    
    // Grupy aktywne
    int grupa1_aktywna;         // 1 jeśli przewodnik 1 ma grupę
    int grupa2_aktywna;         // 1 jeśli przewodnik 2 ma grupę
    
    // PID procesów (do sygnałów)
    pid_t pid_main;
    pid_t pid_kasjer;
    pid_t pid_przewodnik1;
    pid_t pid_przewodnik2;
    pid_t pid_straznik;
    
    // Flagi zamknięcia
    volatile sig_atomic_t zamkniecie_przewodnik1;
    volatile sig_atomic_t zamkniecie_przewodnik2;
    
    int jaskinia_otwarta;  // 1 = otwarta, 0 = zamknięta
    
    // Tablica aktywnych zwiedzających (do ewakuacji)
    pid_t zwiedzajacy_pids[MAX_ZWIEDZAJACYCH];  // MAX_ZWIEDZAJACYCH
    int liczba_aktywnych;
    
    // Czas startu symulacji (dla logów)
    time_t czas_startu;
    
    // === System czasu symulacji ===
    time_t czas_rzeczywisty_start;  // Kiedy symulacja wystartowała (real time)
    int symulowany_czas_sekund;      // Ile sekund minęło w symulacji

    int trasa1_wycieczka_nr;  // Numer bieżącej wycieczki
    int trasa2_wycieczka_nr;
    int trasa1_czeka_na_grupe; // 1 = przewodnik czeka na grupę
    int trasa2_czeka_na_grupe;

    // === KOLEJKI OCZEKUJĄCYCH ===
    pid_t kolejka_trasa1[MAX_ZWIEDZAJACYCH];  // MAX_ZWIEDZAJACYCH
    int kolejka_trasa1_poczatek;
    int kolejka_trasa1_koniec;
    
    pid_t kolejka_trasa2[MAX_ZWIEDZAJACYCH];
    int kolejka_trasa2_poczatek;
    int kolejka_trasa2_koniec;
    
    int para_flaga[MAX_ZWIEDZAJACYCH];  //PARA OPIEKUN+DZIECKO TYLKO NA TRASIE 2

    // Aktualne grupy w trasie (lista PID-ów)
    pid_t grupa1_pids[N1];  // N1
    int grupa1_liczba;
    
    pid_t grupa2_pids[N2];  // N2
    int grupa2_liczba;
    
} StanJaskini;

// KOLEJKA KOMUNIKATÓW (Kasjer ↔ Zwiedzający)

typedef struct {
    long mtype; 
    
    // Dane zwiedzającego (request)
    int zwiedzajacy_pid;
    int wiek;
    int trasa;
    int czy_powrot;             
    
    int jest_opiekunem;          // 1=ten zwiedzający jest opiekunem dziecka
    int pid_opiekuna;            // PID opiekuna (jeśli dziecko)
    int pid_dziecka;             // PID dziecka (jeśli opiekun)
    
    // Odpowiedź kasjera (response)
    int bilet_wydany;
    float cena;
    char powod_odmowy[128];
    
    // Timestamp
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