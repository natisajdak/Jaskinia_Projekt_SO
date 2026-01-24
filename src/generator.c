#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

volatile sig_atomic_t zatrzymaj = 0;

void obsluga_sigterm(int sig) {
    (void)sig;
    zatrzymaj = 1;
}

void obsluga_sigchld(int sig) {
    (void)sig;
    int saved_errno = errno;
    
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    
    errno = saved_errno;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    struct sigaction sa_chld;
    sa_chld.sa_handler = obsluga_sigchld;  
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    sigaction(SIGCHLD, &sa_chld, NULL);

    pid_t moj_pid = getpid();
    srand(time(NULL) ^ moj_pid);
    
    log_info("[GENERATOR] Start (PID: %d)", moj_pid);
    
    signal(SIGTERM, obsluga_sigterm);
    signal(SIGINT, obsluga_sigterm);
    
    int shmid = atoi(getenv("SHMID"));
    int semid = atoi(getenv("SEMID"));
    
    StanJaskini *stan = podlacz_pamiec_dzielona(shmid);
    stan->pid_generator = moj_pid;
    
    log_success("[GENERATOR] Połączono z IPC");
    log_info("[GENERATOR] Będę generować zwiedzających losowo przez %d sekund", 
             CZAS_OTWARCIA_SEK);
    log_info("[GENERATOR] Częstotliwość: co %d-%d sekund", 
             GENERATOR_MIN_DELAY, GENERATOR_MAX_DELAY);
    
    time_t start = time(NULL);
    int licznik_lokalny = 0;
    int blad_fork_count = 0;
    const int MAX_BLAD_FORK = 10;
    
    log_to_file(LOG_GENERATOR, "=== START GENERATORA ===");
    log_to_file(LOG_GENERATOR, "Czas otwarcia: %d sekund (%02d:00 - %02d:00)", 
                CZAS_OTWARCIA_SEK, TP, TK);
    
    // GŁÓWNA PĘTLA
    while (!zatrzymaj && stan->jaskinia_otwarta) {
        // Losowe opóźnienie
        int delay = losuj(GENERATOR_MIN_DELAY, GENERATOR_MAX_DELAY);
        sleep(delay);
        
        int uplynelo = (int)difftime(time(NULL), start);
        if (uplynelo >= CZAS_OTWARCIA_SEK) {
            log_info("[GENERATOR] Upłynął czas otwarcia - przestaję generować");
            break;
        }
        
        if (zatrzymaj || !stan->jaskinia_otwarta) break;
        sem_wait_safe(semid, SEM_WOLNE_SLOTY_ZWIEDZAJACYCH);
        // FORK
        pid_t pid_zwiedzajacy = fork();

        if (pid_zwiedzajacy < 0) {
            perror("fork zwiedzajacy"); 
            blad_fork_count++;

            if (blad_fork_count >= MAX_BLAD_FORK) {
                log_error("[GENERATOR] Zbyt wiele błędów fork() (%d) - zatrzymuję generator", blad_fork_count);
                log_to_file(LOG_GENERATOR, "Zbyt wiele błędów fork() (%d)", blad_fork_count);
                break;
            }

            sleep(2);
            continue;
        }

        blad_fork_count = 0;

        if (pid_zwiedzajacy == 0) {
            execl("./bin/zwiedzajacy", "zwiedzajacy", NULL);
            perror("execl zwiedzajacy");
            _exit(1);
        } else {
            licznik_lokalny++;

            sem_wait_safe(semid, SEM_MUTEX);
            stan->licznik_wygenerowanych++;
            int total = stan->licznik_wygenerowanych;
            sem_signal_safe(semid, SEM_MUTEX);

            log_info("[GENERATOR] Wygenerowano zwiedzającego #%d (PID %d) - łącznie: %d",
                    licznik_lokalny, pid_zwiedzajacy, total);

            log_to_file(LOG_GENERATOR,
                        "Zwiedzający #%d | PID=%d | Czas=%ds od otwarcia",
                        total, pid_zwiedzajacy, uplynelo);
        }

    }
    
    log_info("[GENERATOR] Kończę generowanie - zbieram pozostałe procesy...");
        
        int zebrano = 0;
        int timeout = 10; 
        time_t cleanup_start = time(NULL);
        
        while (difftime(time(NULL), cleanup_start) < timeout) {
            pid_t pid = waitpid(-1, NULL, WNOHANG);
            if (pid > 0) {
                zebrano++;
            } else if (pid == 0) {
                usleep(100000);  
            } else {
                break;
            }
        }
        
        if (zebrano > 0) {
            log_info("[GENERATOR] Zebrano %d procesów zombie podczas cleanup", zebrano);
        }
    
    log_info("[GENERATOR] Kończę generowanie - czekam na wszystkie dzieci...");

    log_info("[GENERATOR] Oczekiwanie na zakończenie %d procesów zwiedzających...", licznik_lokalny);
    
    for (int i = 0; i < licznik_lokalny; i++) {
        sem_wait_safe(semid, SEM_ZAKONCZENI); 
    }

    while (wait(NULL) > 0); 

    log_success("[GENERATOR] Wszystkie procesy zakończone pomyślnie.");
    
    // PODSUMOWANIE
    sem_wait_safe(semid, SEM_MUTEX);
    int total_wygenerowanych = stan->licznik_wygenerowanych;
    sem_signal_safe(semid, SEM_MUTEX);
    
    printf("\n");
    printf(COLOR_BOLD COLOR_MAGENTA);
    printf("╔═══════════════════════════════════════╗\n");
    printf("║      PODSUMOWANIE GENERATORA          ║\n");
    printf("╚═══════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf(COLOR_GREEN "Wygenerowano zwiedzających: %d" COLOR_RESET "\n", total_wygenerowanych);
    printf(COLOR_GREEN "Zakończono poprawnie:       %d" COLOR_RESET "\n", stan->licznik_zakonczonych);
    printf("Czas działania:             %d sekund\n", (int)difftime(time(NULL), start));
    if (total_wygenerowanych > 0) {
        printf("Średnia częstotliwość:      %.1f sek/osoba\n", 
               (float)difftime(time(NULL), start) / total_wygenerowanych);
    }
    printf("\n");
    
    log_to_file(LOG_GENERATOR, "=== KONIEC GENERATORA ===");
    log_to_file(LOG_GENERATOR, "Łącznie wygenerowano: %d zwiedzających", total_wygenerowanych);
    
    log_success("[GENERATOR] Koniec pracy");
    odlacz_pamiec_dzielona(stan);
    
    return 0;
}