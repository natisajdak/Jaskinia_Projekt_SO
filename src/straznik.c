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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    log_info("[STRAŻNIK] Start (PID: %d)", getpid());
    
    // Pobierz IPC
    int shmid = atoi(getenv("SHMID"));
    int semid = atoi(getenv("SEMID"));
    int msgid = atoi(getenv("MSGID"));
    (void)msgid;  // Nie używamy kolejki w strażniku
    
    StanJaskini *stan = podlacz_pamiec_dzielona(shmid);
    stan->pid_straznik = getpid();
    
    log_success("[STRAŻNIK] Połączono z IPC");
    
    // Pobierz PID przewodników
    pid_t pid_przewodnik1 = 0;
    pid_t pid_przewodnik2 = 0;
    
    // Czekaj aż przewodnicy się zarejestrują
    log_info("[STRAŻNIK] Czekam na rejestrację przewodników...");
    while (pid_przewodnik1 == 0 || pid_przewodnik2 == 0) {
        sem_wait_safe(semid, SEM_MUTEX);
        pid_przewodnik1 = stan->pid_przewodnik1;
        pid_przewodnik2 = stan->pid_przewodnik2;
        sem_signal_safe(semid, SEM_MUTEX);
        
        if (pid_przewodnik1 == 0 || pid_przewodnik2 == 0) {
            usleep(100000);  // 100ms
        }
    }
    
    log_success("[STRAŻNIK] Przewodnicy zarejestrowani (PID1: %d, PID2: %d)", 
                pid_przewodnik1, pid_przewodnik2);
    
    // Oblicz czas zamknięcia (Tk - 30 sekund)
    int czas_do_zamkniecia = TK - 30;
    if (czas_do_zamkniecia < 10) {
        czas_do_zamkniecia = 10;  // Minimum 10 sekund
    }
    
    log_info("[STRAŻNIK] Wyślę sygnały zamknięcia za %d sekund (przed Tk=%d)", 
             czas_do_zamkniecia, TK);
    
    // Czekaj do czasu zamknięcia
    time_t start = stan->czas_startu;
    while (czas_od_startu(start) < czas_do_zamkniecia) {
        sleep(1);
        
        // Wyświetl status co 10 sekund
        if (czas_od_startu(start) % 10 == 0) {
            log_info("[STRAŻNIK] Status: bilety=%d, trasa1=%d osób, trasa2=%d osób",
                     stan->bilety_sprzedane, stan->trasa1_licznik, stan->trasa2_licznik);
        }
    }
    
    // === WYSYŁANIE SYGNAŁÓW ZAMKNIĘCIA ===
    
    log_warning("[STRAŻNIK] ZAMYKAM JASKINIĘ - wysyłam sygnały!");
    
    // Oznacz jaskinię jako zamkniętą
    sem_wait_safe(semid, SEM_MUTEX);
    stan->jaskinia_otwarta = 0;
    stan->zamkniecie_przewodnik1 = 1;
    stan->zamkniecie_przewodnik2 = 1;
    sem_signal_safe(semid, SEM_MUTEX);
    
    // Wyślij SIGUSR1 do przewodnika 1
    if (kill(pid_przewodnik1, SIGUSR1) == 0) {
        log_success("[STRAŻNIK] Wysłano SIGUSR1 do przewodnika 1 (PID %d)", pid_przewodnik1);
    } else {
        perror("kill przewodnik1");
        log_error("[STRAŻNIK] Błąd wysyłania sygnału do przewodnika 1");
    }
    
    sleep(1);  // Krótka przerwa
    
    // Wyślij SIGUSR2 do przewodnika 2
    if (kill(pid_przewodnik2, SIGUSR2) == 0) {
        log_success("[STRAŻNIK] Wysłano SIGUSR2 do przewodnika 2 (PID %d)", pid_przewodnik2);
    } else {
        perror("kill przewodnik2");
        log_error("[STRAŻNIK] Błąd wysyłania sygnału do przewodnika 2");
    }
    
    log_info("[STRAŻNIK] Sygnały zamknięcia wysłane");
    
    // Czekaj na zakończenie wycieczek w trakcie
    log_info("[STRAŻNIK] Czekam na zakończenie wycieczek w trakcie...");
    
    int max_oczekiwanie = (T1 > T2 ? T1 : T2) + 30;  // Max czas + bufor
    int oczekiwanie = 0;
    
    while (oczekiwanie < max_oczekiwanie) {
        sem_wait_safe(semid, SEM_MUTEX);
        int grupa1 = stan->grupa1_aktywna;
        int grupa2 = stan->grupa2_aktywna;
        int trasa1_osoby = stan->trasa1_licznik;
        int trasa2_osoby = stan->trasa2_licznik;
        sem_signal_safe(semid, SEM_MUTEX);
        
        if (!grupa1 && !grupa2 && trasa1_osoby == 0 && trasa2_osoby == 0) {
            log_success("[STRAŻNIK] Wszystkie wycieczki zakończone, jaskinia pusta!");
            break;
        }
        
        if (oczekiwanie % 5 == 0) {
            log_info("[STRAŻNIK] Oczekiwanie %d/%d sek (trasa1: %d osób %s, trasa2: %d osób %s)",
                     oczekiwanie, max_oczekiwanie,
                     trasa1_osoby, grupa1 ? "[AKTYWNA]" : "",
                     trasa2_osoby, grupa2 ? "[AKTYWNA]" : "");
        }
        
        sleep(1);
        oczekiwanie++;
    }
    
    if (oczekiwanie >= max_oczekiwanie) {
        log_warning("[STRAŻNIK] Timeout - niektóre wycieczki mogły nie zakończyć się");
    }
    
    // === EWAKUACJA POZOSTAŁYCH ZWIEDZAJĄCYCH ===
    
    sem_wait_safe(semid, SEM_MUTEX);
    int liczba_aktywnych = stan->liczba_aktywnych;
    sem_signal_safe(semid, SEM_MUTEX);
    
    if (liczba_aktywnych > 0) {
        log_warning("[STRAŻNIK] Wykryto %d aktywnych zwiedzających - EWAKUACJA!",
                    liczba_aktywnych);
        
        printf("\n");
        printf(COLOR_RED COLOR_BOLD "╔═══════════════════════════════════════════╗\n");
        printf("║           PROCEDURA EWAKUACJI             ║\n");
        printf("╚═══════════════════════════════════════════╝\n" COLOR_RESET);
        
        int ewakuowanych = 0;
        
        for (int i = 0; i < liczba_aktywnych && i < MAX_ZWIEDZAJACYCH; i++) {
            sem_wait_safe(semid, SEM_MUTEX);
            pid_t pid = stan->zwiedzajacy_pids[i];
            sem_signal_safe(semid, SEM_MUTEX);
            
            if (pid > 0) {
                log_info("[STRAŻNIK] → Ewakuuję zwiedzającego PID %d (SIGTERM)", pid);
                
                if (kill(pid, SIGTERM) == 0) {
                    ewakuowanych++;
                } else {
                    // Proces może już się zakończyć
                    if (errno != ESRCH) {
                        perror("kill SIGTERM");
                    }
                }
                
                usleep(100000);  // 100ms przerwy między sygnałami
            }
        }
        
        log_success("[STRAŻNIK] Ewakuowano %d zwiedzających", ewakuowanych);
        
        // Daj czas na wyjście (max 5 sekund)
        log_info("[STRAŻNIK] Oczekiwanie na opuszczenie jaskini...");
        
        for (int t = 0; t < 50; t++) {  // 50 x 100ms = 5 sekund
            sem_wait_safe(semid, SEM_MUTEX);
            int pozostalo = stan->liczba_aktywnych;
            sem_signal_safe(semid, SEM_MUTEX);
            
            if (pozostalo == 0) {
                log_success("[STRAŻNIK] Wszyscy zwiedzający opuścili jaskinię!");
                break;
            }
            
            usleep(100000);  // 100ms
        }
        
        // Sprawdź końcowy stan
        sem_wait_safe(semid, SEM_MUTEX);
        int pozostalo = stan->liczba_aktywnych;
        sem_signal_safe(semid, SEM_MUTEX);
        
        if (pozostalo > 0) {
            log_warning("[STRAŻNIK] Pozostało %d zwiedzających (timeout ewakuacji)", pozostalo);
        }
        
        printf("\n");
    } else {
        log_success("[STRAŻNIK] Jaskinia była pusta - brak ewakuacji");
    }

    // === PODSUMOWANIE ===
    
    printf("\n");
    printf(COLOR_BOLD COLOR_YELLOW);
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║       PODSUMOWANIE STRAŻNIKA              ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    
    sem_wait_safe(semid, SEM_MUTEX);
    printf("Bilety sprzedane:     %d\n", stan->bilety_sprzedane);
    printf("  - Trasa 1:          %d\n", stan->bilety_trasa1);
    printf("  - Trasa 2:          %d\n", stan->bilety_trasa2);
    printf("Osoby na trasie 1:    %d\n", stan->trasa1_licznik);
    printf("Osoby na trasie 2:    %d\n", stan->trasa2_licznik);
    printf("Aktywni zwiedzający:  %d\n", stan->liczba_aktywnych);
    sem_signal_safe(semid, SEM_MUTEX);
    
    printf("\n");
    
    // Log do pliku
    zapisz_log_symulacji("=== RAPORT STRAŻNIKA ===");
    zapisz_log_symulacji("Bilety sprzedane: %d (Trasa1: %d, Trasa2: %d)",
                         stan->bilety_sprzedane, stan->bilety_trasa1, stan->bilety_trasa2);
    zapisz_log_symulacji("Sygnały zamknięcia wysłane o czasie: %d sekund od startu", 
                         czas_do_zamkniecia);
    zapisz_log_symulacji("Jaskinia bezpiecznie zamknięta");
    
    log_success("[STRAŻNIK] Koniec pracy");
    
    odlacz_pamiec_dzielona(stan);
    
    return 0;
}
