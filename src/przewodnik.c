#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Parametry przewodnika
int numer_trasy;  // 1 lub 2
int shmid_global, semid_global;
StanJaskini *stan_global = NULL;

// Flaga zamknięcia (sygnał)
volatile sig_atomic_t flaga_zamkniecie = 0;

// Obsługa sygnału zamknięcia
void obsluga_zamkniecia(int sig) {
    if (sig == SIGUSR1 || sig == SIGUSR2) {
        flaga_zamkniecie = 1;
        log_warning("[PRZEWODNIK %d] Otrzymano sygnał zamknięcia!", numer_trasy);
    }
}

// WEJŚCIE PRZEZ KŁADKĘ 
void wpusc_przez_kladke(int liczba_osob) {
    int sem_kladka = (numer_trasy == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int pozostalo = liczba_osob;
    int numer_osoby_w_grupie = 0;

    log_info("[PRZEWODNIK %d] Wpuszczam %d osób przez kładkę (max %d jednocześnie)...", 
             numer_trasy, liczba_osob, K);

    while (pozostalo > 0) {
        // Oblicz wielkość tury (nie więcej niż K i nie więcej niż zostało osób)
        int tura = (pozostalo > K) ? K : pozostalo;

        // 1. WEJŚCIE TURY: Rezerwujemy miejsca i wchodzimy
        for (int j = 0; j < tura; j++) {
            sem_wait_safe(semid_global, sem_kladka); // Czekaj na wolne miejsce na kładce
            
            sem_wait_safe(semid_global, SEM_MUTEX);
            numer_osoby_w_grupie++;
            if (numer_trasy == 1) stan_global->kladka1_licznik++;
            else stan_global->kladka2_licznik++;
            
            int aktualnie_na_kladce = (numer_trasy == 1) ? stan_global->kladka1_licznik : stan_global->kladka2_licznik;
            log_info("[PRZEWODNIK %d] Osoba %d/%d wchodzi na kładkę (na kładce: %d/%d)", 
                     numer_trasy, numer_osoby_w_grupie, liczba_osob, aktualnie_na_kladce, K);
            sem_signal_safe(semid_global, SEM_MUTEX);
        }

        // 2. PRZEJŚCIE: Cała tura idzie przez kładkę w tym samym czasie
        log_info("[PRZEWODNIK %d] Tura %d osób przechodzi przez kładkę...", numer_trasy, tura);
        usleep(losuj(1000000, 1500000)); // Symulacja czasu przejścia tury

        // 3. ZEJŚCIE TURY: Osoby schodzą z kładki na trasę
        for (int j = 0; j < tura; j++) {
            sem_wait_safe(semid_global, SEM_MUTEX);
            if (numer_trasy == 1) {
                stan_global->kladka1_licznik--;
                stan_global->trasa1_licznik++; 
            } else {
                stan_global->kladka2_licznik--;
                stan_global->trasa2_licznik++;
            }
            sem_signal_safe(semid_global, SEM_MUTEX);
            
            sem_signal_safe(semid_global, sem_kladka); // Zwolnij miejsce dla następnych
        }

        pozostalo -= tura;
        if (pozostalo > 0) log_info("[PRZEWODNIK %d] Kolejna tura...", numer_trasy);
    }
    log_success("[PRZEWODNIK %d] Wszyscy weszli na trasę", numer_trasy);
}
// WYJŚCIE PRZEZ KŁADKĘ 
void wypusc_przez_kladke(int liczba_osob) {
    int sem_kladka = (numer_trasy == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int pozostalo = liczba_osob;
    int numer_osoby_w_grupie = 0;

    log_info("[PRZEWODNIK %d] Wypuszczam %d osób z jaskini (max %d na kładce)...", 
             numer_trasy, liczba_osob, K);

    while (pozostalo > 0) {
        int tura = (pozostalo > K) ? K : pozostalo;

        // 1. WEJŚCIE NA KŁADKĘ (od strony jaskini)
        for (int j = 0; j < tura; j++) {
            sem_wait_safe(semid_global, sem_kladka);
            
            sem_wait_safe(semid_global, SEM_MUTEX);
            numer_osoby_w_grupie++;
            if (numer_trasy == 1) {
                stan_global->kladka1_licznik++;
                stan_global->trasa1_licznik--;
            } else {
                stan_global->kladka2_licznik++;
                stan_global->trasa2_licznik--;
            }
            int aktualnie = (numer_trasy == 1) ? stan_global->kladka1_licznik : stan_global->kladka2_licznik;
            log_info("[PRZEWODNIK %d] Osoba %d/%d opuszcza trasę (na kładce: %d/%d)", 
                     numer_trasy, numer_osoby_w_grupie, liczba_osob, aktualnie, K);
            sem_signal_safe(semid_global, SEM_MUTEX);
        }

        // 2. PRZEJŚCIE TURY
        usleep(losuj(1000000, 1500000));

        // 3. ZEJŚCIE Z KŁADKI (na zewnątrz)
        for (int j = 0; j < tura; j++) {
            sem_wait_safe(semid_global, SEM_MUTEX);
            if (numer_trasy == 1) stan_global->kladka1_licznik--;
            else stan_global->kladka2_licznik--;
            sem_signal_safe(semid_global, SEM_MUTEX);
            
            sem_signal_safe(semid_global, sem_kladka);
        }

        pozostalo -= tura;
    }
    log_success("[PRZEWODNIK %d] Wszyscy wyszli na zewnątrz", numer_trasy);
}

// ZARZĄDZANIE MIEJSCEM NA TRASIE (Limity Ni)
void zajmij_miejsce_na_trasie(int liczba_osob) {
    int sem_limit = (numer_trasy == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;
    int max_limit = (numer_trasy == 1) ? N1 : N2;
    
    log_info("[PRZEWODNIK %d] Rezerwuję %d miejsc na trasie...", numer_trasy, liczba_osob);
    
    for (int i = 0; i < liczba_osob; i++) {
        sem_wait_safe(semid_global, sem_limit); 
    }
    
    log_success("[PRZEWODNIK %d] Miejsca zarezerwowane (%d/%d)", numer_trasy, liczba_osob, max_limit);
}

void zwolnij_miejsce_na_trasie(int liczba_osob) {
    int sem_limit = (numer_trasy == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;
    
    for (int i = 0; i < liczba_osob; i++) {
        sem_signal_safe(semid_global, sem_limit); 
    }
    log_info("[PRZEWODNIK %d] Zwolniono semafory trasy (%d miejsc)", numer_trasy, liczba_osob);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Użycie: %s <numer_trasy>\n", argv[0]);
        return 1;
    }
    
    numer_trasy = atoi(argv[1]);
    if (numer_trasy != 1 && numer_trasy != 2) return 1;
    
    log_info("[PRZEWODNIK %d] Startuje (PID: %d)", numer_trasy, getpid());
    
    shmid_global = atoi(getenv("SHMID"));
    semid_global = atoi(getenv("SEMID"));
    stan_global = podlacz_pamiec_dzielona(shmid_global);
    
    if (numer_trasy == 1) stan_global->pid_przewodnik1 = getpid();
    else stan_global->pid_przewodnik2 = getpid();
    
    signal(SIGUSR1, obsluga_zamkniecia);
    signal(SIGUSR2, obsluga_zamkniecia);
    
    int czas_trasy = (numer_trasy == 1) ? T1 : T2;
    int numer_wycieczki = 0;
    
    while (!flaga_zamkniecie && stan_global->jaskinia_otwarta) {
        numer_wycieczki++;
        
        log_info("[PRZEWODNIK %d] ════════════ WYCIECZKA #%d ════════════", numer_trasy, numer_wycieczki);
        
        int max_na_trasie = (numer_trasy == 1 ? N1 : N2);
        int liczba_w_grupie = losuj(3, max_na_trasie);
        
        log_info("[PRZEWODNIK %d] Grupa zebrana: %d osób", numer_trasy, liczba_w_grupie);
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2, 
                    "Wycieczka #%d: START (grupa: %d osób)", numer_wycieczki, liczba_w_grupie);

        if (flaga_zamkniecie) break;

        // KROK 1: Rezerwacja biletów (semafor pojemności trasy)
        zajmij_miejsce_na_trasie(liczba_w_grupie);
        
        // KROK 2: Fizyczne wpuszczanie przez kładkę
        wpusc_przez_kladke(liczba_w_grupie);
        
        // KROK 3: Zwiedzanie
        log_info("[PRZEWODNIK %d] Trasa w toku (czas: %ds)...", numer_trasy, czas_trasy);
        
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) stan_global->grupa1_aktywna = 1;
        else stan_global->grupa2_aktywna = 1;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        sleep(czas_trasy);
        
        // KROK 4: Powrót przez kładkę
        wypusc_przez_kladke(liczba_w_grupie);
        
        // KROK 5: Oddanie "biletów" (zwolnienie miejsca na trasie)
        zwolnij_miejsce_na_trasie(liczba_w_grupie);
        
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) stan_global->grupa1_aktywna = 0;
        else stan_global->grupa2_aktywna = 0;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2, 
                    "Wycieczka #%d: KONIEC", numer_wycieczki);
        
        log_success("[PRZEWODNIK %d] Wycieczka #%d zakończona sukcesem", numer_trasy, numer_wycieczki);
        
        if (flaga_zamkniecie) break;
        sleep(2); // Odpoczynek przewodnika
    }
    
    log_info("[PRZEWODNIK %d] Zakończono prowadzenie wycieczek", numer_trasy);
    log_success("[PRZEWODNIK %d] Łącznie przeprowadzono: %d wycieczek", numer_trasy, numer_wycieczki);
    
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║   PODSUMOWANIE PRZEWODNIKA TRASY %d        ║\n", numer_trasy);
    printf("╚═══════════════════════════════════════════╝\n");
    printf("Przeprowadzono wycieczek: %d\n", numer_wycieczki);
    printf("Czas trasy:               %d sekund\n", czas_trasy);
    printf("Max osób na trasie:       %d\n", (numer_trasy == 1 ? N1 : N2));
    printf("Max osób na kładce:       %d\n", K);
    printf("\n");
    
    odlacz_pamiec_dzielona(stan_global);
    
    return 0;
}
 
    
    

