#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Zmienne globalne dla łatwiejszego dostępu w funkcjach
int shmid, semid;
StanJaskini *stan = NULL;

void wyslij_sygnaly_zamkniecia() {
    log_warning("[STRAŻNIK] ZAMYKAM JASKINIĘ - wysyłam sygnały!");

    sem_wait_safe(semid, SEM_MUTEX);
    stan->jaskinia_otwarta = 0;
    stan->zamkniecie_przewodnik1 = 1;
    stan->zamkniecie_przewodnik2 = 1;
    pid_t p1 = stan->pid_przewodnik1;
    pid_t p2 = stan->pid_przewodnik2;
    pid_t pk = stan->pid_kasjer;
    sem_signal_safe(semid, SEM_MUTEX);

    sem_signal_safe(semid, SEM_KOLEJKA1_NIEPUSTA);
    sem_signal_safe(semid, SEM_KOLEJKA2_NIEPUSTA);

    // Sygnał dla Przewodnika 1
    if (p1 > 0) {
        if (kill(p1, SIGUSR1) == 0) {
            log_success("[STRAŻNIK] Wysłano SIGUSR1 do przewodnika 1 (PID %d)", p1);
        } else {
            log_error("[STRAŻNIK] Błąd sygnału do przewodnika 1: %s", strerror(errno));
        }
    }

    usleep(200000); // 200ms przerwy

    // Sygnał dla Przewodnika 2
    if (p2 > 0) {
        if (kill(p2, SIGUSR2) == 0) {
            log_success("[STRAŻNIK] Wysłano SIGUSR2 do przewodnika 2 (PID %d)", p2);
        } else {
            log_error("[STRAŻNIK] Błąd sygnału do przewodnika 2: %s", strerror(errno));
        }
    }

    // Sygnał dla Kasjera
    if (pk > 0) {
        if (kill(pk, SIGTERM) == 0) {
            log_success("[STRAŻNIK] Wysłano SIGTERM do kasjera (PID %d)", pk);
        }
    }

    log_info("[STRAŻNIK] Sygnały zamknięcia wysłane");
}

void czekaj_na_zakonczenie_wycieczek() {
    log_info("[STRAŻNIK] Czekam na zakończenie wycieczek w trakcie...");
    
    int max_timeout = (T1 > T2 ? T1 : T2) + 20;
    int timeout = 0;
    
    while (timeout < max_timeout) {
        sem_wait_safe(semid, SEM_MUTEX);
        int trasa1_osoby = stan->trasa1_licznik;
        int trasa2_osoby = stan->trasa2_licznik;
        int g1_aktywna = stan->grupa1_aktywna;
        int g2_aktywna = stan->grupa2_aktywna;
        sem_signal_safe(semid, SEM_MUTEX);
        
        if (trasa1_osoby == 0 && trasa2_osoby == 0 && !g1_aktywna && !g2_aktywna) {
            log_success("[STRAŻNIK] Wszystkie wycieczki zakończone, jaskinia pusta!");
            break;
        }
        
        if (timeout % 5 == 0) {
            log_info("[STRAŻNIK] Oczekiwanie %d/%d sek (trasa1: %d osób%s, trasa2: %d osób%s)",
                     timeout, max_timeout,
                     trasa1_osoby, g1_aktywna ? " [AKTYWNA]" : "",
                     trasa2_osoby, g2_aktywna ? " [AKTYWNA]" : "");
        }
        
        sleep(1);
        timeout++;
    }
    
    if (timeout >= max_timeout) {
        log_warning("[STRAŻNIK] Timeout - grupy nie opuściły jaskini w wyznaczonym czasie!");
    }
    
    log_warning("[STRAŻNIK] Ewakuacja wszystkich zwiedzających...");
    sem_wait_safe(semid, SEM_MUTEX); 
    int ewakuowani = 0;
    for (int i = 0; i < 100; i++) {
        if (stan->zwiedzajacy_pids[i] > 0) {
            kill(stan->zwiedzajacy_pids[i], SIGTERM);
            ewakuowani++;
        }
    }
    sem_signal_safe(semid, SEM_MUTEX);

    if (ewakuowani > 0) {
        log_warning("[STRAŻNIK] Ewakuowano %d zwiedzających", ewakuowani);
        sleep(2);  // Daj czas na reakcję
    }
}

int main() {
    log_info("[STRAŻNIK] Start (PID: %d)", getpid());
    
    char *shmid_env = getenv("SHMID");
    char *semid_env = getenv("SEMID");
    if (!shmid_env || !semid_env) {
        log_error("[STRAŻNIK] Brak zmiennych środowiskowych IPC!");
        return 1;
    }

    shmid = atoi(shmid_env);
    semid = atoi(semid_env);
    stan = podlacz_pamiec_dzielona(shmid);
    
    log_success("[STRAŻNIK] Połączono z IPC");
    stan->pid_straznik = getpid();
    
    // Czekaj na rejestrację przewodników
    log_info("[STRAŻNIK] Czekam na rejestrację przewodników...");
    while (1) {
        sem_wait_safe(semid, SEM_MUTEX);
        int p1 = stan->pid_przewodnik1;
        int p2 = stan->pid_przewodnik2;
        sem_signal_safe(semid, SEM_MUTEX);
        if (p1 > 0 && p2 > 0) break;
        usleep(200000);
    }
    log_success("[STRAŻNIK] Przewodnicy zarejestrowani");
    
    wyslij_sygnaly_zamkniecia();
    czekaj_na_zakonczenie_wycieczek();
    
    // Podsumowanie
    printf("\n");
    printf("\n" COLOR_BOLD COLOR_YELLOW);
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║         PODSUMOWANIE STRAŻNIKA            ║\n");
    printf("╚═══════════════════════════════════════════╝\n" COLOR_RESET);
    
    sem_wait_safe(semid, SEM_MUTEX);
    printf("Bilety sprzedane:      %d\n", stan->bilety_sprzedane);
    printf("  - Trasa 1:           %d\n", stan->bilety_trasa1);
    printf("  - Trasa 2:           %d\n", stan->bilety_trasa2);
    printf("Osoby na trasie 1:     %d\n", stan->trasa1_licznik);
    printf("Osoby na trasie 2:     %d\n", stan->trasa2_licznik);
    sem_signal_safe(semid, SEM_MUTEX);

    printf("\n");
    
    // POPRAWKA: Policz tylko tych co są FAKTYCZNIE aktywni (nie w kolejce)
    int faktycznie_aktywni = 0;
    for(int i=0; i<100; i++) {
        if(stan->zwiedzajacy_pids[i] > 0) faktycznie_aktywni++;
    }
    
    int w_kolejkach = stan->kolejka_trasa1_koniec + stan->kolejka_trasa2_koniec;
    
    printf("W kolejkach:           %d\n", w_kolejkach);
    printf("Faktycznie aktywni:    %d\n", faktycznie_aktywni);
    
    if (w_kolejkach > 0) {
        log_info("[STRAŻNIK] %d zwiedzających czekało w kolejkach - zostali ewakuowani", w_kolejkach);
    }
    if (faktycznie_aktywni > 0) {
        log_info("[STRAŻNIK] %d zwiedzających pozostało aktywnych - zostali ewakuowani", faktycznie_aktywni);
    }
    
    printf("\n");
    
    zapisz_log_symulacji("=== RAPORT STRAŻNIKA ===");
    zapisz_log_symulacji("Bilety: %d (T1: %d, T2: %d), W kolejkach: %d, Aktywni: %d", 
                         stan->bilety_sprzedane, stan->bilety_trasa1, stan->bilety_trasa2, 
                         w_kolejkach, faktycznie_aktywni);

    log_success("[STRAŻNIK] Koniec pracy");
    odlacz_pamiec_dzielona(stan);
    return 0;
}
