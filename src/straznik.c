#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
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

    // Obudź wszystkich czekających
    for (int i = 0; i < MAX_ZWIEDZAJACYCH_TABLICA; i++) {
    sem_signal_safe(semid, SEM_KOLEJKA1_NIEPUSTA);
    sem_signal_safe(semid, SEM_KOLEJKA2_NIEPUSTA);
    sem_signal_safe(semid, SEM_PRZEWODNIK1_READY);
    sem_signal_safe(semid, SEM_PRZEWODNIK2_READY);
    }

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

    int max_timeout = (T1 > T2 ? T1 : T2) + 10 + (K * 3);
    int timeout = 0;

    while (timeout < max_timeout) {
        sem_wait_safe(semid, SEM_MUTEX);

        int trasa1_osoby = stan->trasa1_licznik;
        int trasa2_osoby = stan->trasa2_licznik;
        int g1_aktywna   = stan->grupa1_aktywna;
        int g2_aktywna   = stan->grupa2_aktywna;
        int w_kolejkach  = stan->kolejka_trasa1_koniec + stan->kolejka_trasa2_koniec;
        int aktywnych    = stan->liczba_aktywnych;

        sem_signal_safe(semid, SEM_MUTEX);

        if (trasa1_osoby == 0 && trasa2_osoby == 0 &&
            !g1_aktywna && !g2_aktywna &&
            w_kolejkach == 0 && aktywnych == 0) {

            log_success("[STRAŻNIK] Wszystkie wycieczki zakończone, jaskinia pusta!");
            log_success("[STRAŻNIK] Godzina zamknięcia: %02d:00 (symulowane)", TK);
            return;
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

    sem_wait_safe(semid, SEM_MUTEX);
    int ktos_jest = (stan->liczba_aktywnych > 0 ||
                     stan->trasa1_licznik > 0 ||
                     stan->trasa2_licznik > 0 ||
                     stan->kolejka_trasa1_koniec > 0 ||
                     stan->kolejka_trasa2_koniec > 0);
    sem_signal_safe(semid, SEM_MUTEX);

    if (!ktos_jest) {
        log_success("[STRAŻNIK] Timeout, ale jaskinia pusta – kończę bez ewakuacji");
        return;
    }

    log_warning("[STRAŻNIK] Timeout – ewakuacja pozostałych zwiedzających!");

    // EWAKUACJA 
    sem_wait_safe(semid, SEM_MUTEX);
    int ewakuowani = 0;

    for (int i = 0; i < MAX_ZWIEDZAJACYCH_TABLICA; i++) {
        pid_t pid = stan->zwiedzajacy_pids[i];
        if (pid > 0 && kill(pid, 0) == 0) {
            kill(pid, SIGTERM);
            ewakuowani++;
        }
    }
    sem_signal_safe(semid, SEM_MUTEX);

    if (ewakuowani > 0) {
        log_warning("[STRAŻNIK] Wysłano SIGTERM do %d zwiedzających", ewakuowani);
        sleep(5);

        /* SIGKILL dla opornych */
        sem_wait_safe(semid, SEM_MUTEX);
        for (int i = 0; i < MAX_ZWIEDZAJACYCH_TABLICA; i++) {
            pid_t pid = stan->zwiedzajacy_pids[i];
            if (pid > 0 && kill(pid, 0) == 0) {
                log_warning("[STRAŻNIK] Proces %d nie zareagował – SIGKILL", pid);
                kill(pid, SIGKILL);
            }
        }
        sem_signal_safe(semid, SEM_MUTEX);
    }

    // SPRZĄTANIE STANU 
    sem_wait_safe(semid, SEM_MUTEX);

    stan->trasa1_licznik = 0;
    stan->trasa2_licznik = 0;
    stan->kladka1_licznik = 0;
    stan->kladka2_licznik = 0;
    stan->grupa1_aktywna = 0;
    stan->grupa2_aktywna = 0;
    stan->liczba_aktywnych = 0;

    sem_signal_safe(semid, SEM_MUTEX);

    log_info("[STRAŻNIK] Stan wyczyszczony po ewakuacji");

    for (int i = 0; i < K; i++) {
        sem_signal_safe(semid, SEM_KLADKA1);
        sem_signal_safe(semid, SEM_KLADKA2);
    }
    for (int i = 0; i < N1; i++) sem_signal_safe(semid, SEM_TRASA1_LIMIT);
    for (int i = 0; i < N2; i++) sem_signal_safe(semid, SEM_TRASA2_LIMIT);
}


void monitoruj_jaskinie() { 
    int czas_do_zamkniecia = CZAS_OTWARCIA_SEK - 2;
    if (czas_do_zamkniecia < 2) czas_do_zamkniecia = 2;

    log_info("[STRAŻNIK] Wyślę sygnały zamknięcia za ok. %d sekund", czas_do_zamkniecia);
    log_info("[STRAŻNIK] Jaskinia otwarta: %02d:00, zamknie się: %02d:00 (symulowane)", TP, TK);
    
    sem_wait_safe(semid, SEM_MUTEX);
    time_t start = stan->czas_startu;
    sem_signal_safe(semid, SEM_MUTEX);
    
    while (czas_od_startu(start) < czas_do_zamkniecia && stan->jaskinia_otwarta) {
        usleep(10000);
    }

    log_info("[STRAŻNIK] Czas zamknięcia - inicjuję procedurę");
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
    int timeout = 0;
    while (timeout < 10) {
        sem_wait_safe(semid, SEM_MUTEX);
        int p1 = stan->pid_przewodnik1;
        int p2 = stan->pid_przewodnik2;
        sem_signal_safe(semid, SEM_MUTEX);
        if (p1 > 0 && p2 > 0) break;
        usleep(200000);
        timeout++;
    }
    
    if (timeout >= 10) {
        log_warning("[STRAŻNIK] Timeout oczekiwania na przewodników!");
        return 1;   
    } else {
        log_success("[STRAŻNIK] Przewodnicy zarejestrowani");
    }
    
    monitoruj_jaskinie();
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
    
    int w_kolejkach = stan->kolejka_trasa1_koniec + stan->kolejka_trasa2_koniec;
    
    printf("W kolejkach:           %d\n", w_kolejkach);
    if (w_kolejkach > 0) {
        log_info("[STRAŻNIK] %d zwiedzających czekało w kolejkach - zostali ewakuowani", w_kolejkach);
    }
    
    printf("\n");
    
    zapisz_log_symulacji("=== RAPORT STRAŻNIKA ===");
    zapisz_log_symulacji("Bilety: %d (T1: %d, T2: %d), W kolejkach: %d, Aktywni: %d", 
                         stan->bilety_sprzedane, stan->bilety_trasa1, stan->bilety_trasa2, 
                         w_kolejkach);

    log_success("[STRAŻNIK] Koniec pracy");
    odlacz_pamiec_dzielona(stan);
    return 0;
}