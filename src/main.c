#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Globalne ID struktur IPC
int shmid_global = -1;
int semid_global = -1;
int msgid_global = -1;
StanJaskini *stan_global = NULL;

// Lista PID procesów potomnych (do czekania)
pid_t pids[100];
int liczba_procesow = 0;

// Funkcja sprzątająca
void cleanup(void) {
    log_info("Sprzątanie zasobów...");
    
    if (stan_global) {
        // Wypisz końcowy stan
        wypisz_stan_jaskini(stan_global);
        
        // Zapisz podsumowanie
        zapisz_log_symulacji("=== KONIEC SYMULACJI ===");
        zapisz_log_symulacji("Bilety sprzedane: %d (Trasa1: %d, Trasa2: %d)",
                             stan_global->bilety_sprzedane,
                             stan_global->bilety_trasa1,
                             stan_global->bilety_trasa2);
        
        odlacz_pamiec_dzielona(stan_global);
    }
    
    if (shmid_global >= 0) {
        usun_pamiec_dzielona(shmid_global);
    }
    
    if (semid_global >= 0) {
        usun_semafory(semid_global);
    }
    
    if (msgid_global >= 0) {
        usun_kolejke(msgid_global);
    }
    
    log_success("Zasoby zwolnione");
}

// Obsługa Ctrl+C
void obsluga_sigint(int sig) {
    (void)sig;
    printf("\n");
    log_warning("╔═══════════════════════════════════════╗");
    log_warning("║    OTRZYMANO SIGINT (Ctrl+C)          ║");
    log_warning("║    AWARYJNE ZAMYKANIE JASKINI...      ║");
    log_warning("╚═══════════════════════════════════════╝");

    if (stan_global) {
        sem_wait_safe(semid_global, SEM_MUTEX);
        stan_global->zamkniecie_przewodnik1 = 1;
        stan_global->zamkniecie_przewodnik2 = 1;
        stan_global->jaskinia_otwarta = 0;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_warning("→ Jaskinia oznaczona jako ZAMKNIĘTA");
        
        // Wyślij sygnały do przewodników
        if (stan_global->pid_przewodnik1 > 0) {
            log_info("→ Wysyłam SIGUSR1 do przewodnika 1 (PID %d)", stan_global->pid_przewodnik1);
            kill(stan_global->pid_przewodnik1, SIGUSR1);
        }
        
        if (stan_global->pid_przewodnik2 > 0) {
            log_info("→ Wysyłam SIGUSR2 do przewodnika 2 (PID %d)", stan_global->pid_przewodnik2);
            kill(stan_global->pid_przewodnik2, SIGUSR2);
        }
        
        // Ewakuuj zwiedzających
        log_warning("→ Ewakuacja aktywnych zwiedzających...");
        
        int ewakuowani = 0;
        for (int i = 0; i < MAX_ZWIEDZAJACYCH; i++) {
            if (stan_global->zwiedzajacy_pids[i] > 0) {
                kill(stan_global->zwiedzajacy_pids[i], SIGTERM);
                ewakuowani++;
            }
        }
        
        log_success("→ Wysłano sygnały ewakuacji do %d zwiedzających", ewakuowani);
    }

    // Zakończ wszystkie procesy potomne
    log_info("→ Zakańczam procesy potomne...");
    for (int i = 0; i < liczba_procesow; i++) {
        if (pids[i] > 0) {
            kill(pids[i], SIGTERM);
        }
    }
    
    sleep(2);
    
    log_warning("→ Wymuszam zakończenie pozostałych procesów...");
    for (int i = 0; i < liczba_procesow; i++) {
        if (pids[i] > 0) {
            kill(pids[i], SIGKILL);
        }
    }
    
    log_success("AWARYJNE ZAMKNIĘCIE ZAKOŃCZONE");
    
    exit(0);
}

// Inicjalizacja struktur IPC
void inicjalizuj_ipc(void) {
    log_info("Inicjalizacja struktur IPC...");
    
    // Pamięć dzielona
    shmid_global = utworz_pamiec_dzielona();
    stan_global = podlacz_pamiec_dzielona(shmid_global);
    
    // Wyzeruj stan
    memset(stan_global, 0, sizeof(StanJaskini));
    stan_global->czas_startu = time(NULL);
    stan_global->pid_main = getpid();
    stan_global->jaskinia_otwarta = 1;  // WAŻNE!
    
    log_success("Pamięć dzielona zainicjalizowana");
    
    // Semafory
    semid_global = utworz_semafory();
    inicjalizuj_semafory(semid_global);
    
    // Kolejka komunikatów
    msgid_global = utworz_kolejke();
    
    log_success("Wszystkie struktury IPC gotowe");
}

// Stwórz pliki logów
void utworz_logi(void) {
    system("mkdir -p logs");
    
    // Wyczyść stare logi
    unlink(LOG_BILETY);
    unlink(LOG_TRASA1);
    unlink(LOG_TRASA2);
    unlink(LOG_SYMULACJA);
    
    // Nagłówki
    time_t now = time(NULL);
    log_to_file(LOG_SYMULACJA, "=== START SYMULACJI JASKINIA ===");
    log_to_file(LOG_SYMULACJA, "Data: %s", ctime(&now));
    log_to_file(LOG_SYMULACJA, "Parametry: N1=%d, N2=%d, K=%d, T1=%d, T2=%d",
                N1, N2, K, T1, T2);
    
    log_to_file(LOG_BILETY, "PID | Wiek | Trasa | Powrót | Cena | Status | Info");
    log_to_file(LOG_TRASA1, "=== LOGI PRZEWODNIKA TRASY 1 ===");
    log_to_file(LOG_TRASA2, "=== LOGI PRZEWODNIKA TRASY 2 ===");
    
    log_success("Pliki logów utworzone");
}

// Ustawia zmienne środowiskowe z ID struktur IPC
void ustaw_env_ipc(void) {
    char buf[32];
    
    snprintf(buf, sizeof(buf), "%d", shmid_global);
    setenv("SHMID", buf, 1);
    
    snprintf(buf, sizeof(buf), "%d", semid_global);
    setenv("SEMID", buf, 1);
    
    snprintf(buf, sizeof(buf), "%d", msgid_global);
    setenv("MSGID", buf, 1);
}

// Dodaj PID do listy
void dodaj_pid(pid_t pid) {
    if (liczba_procesow < 100) {
        pids[liczba_procesow++] = pid;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("\n");
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║                 JASKINIA                  ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("\n");
    
    // Seed dla rand()
    srand(time(NULL));
    
    // Rejestracja cleanup przy wyjściu
    atexit(cleanup);
    
    // Obsługa Ctrl+C
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));           // Zerowanie struktury dla pewności
    sa.sa_handler = obsluga_sigint;       // Wskazanie funkcji obsługi
    sigemptyset(&sa.sa_mask);             // Nie blokujemy dodatkowych sygnałów podczas obsługi
    sa.sa_flags = 0;                      // Flagi (0 oznacza brak specjalnych zachowań jak SA_RESTART)

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Inicjalizacja
    inicjalizuj_ipc();
    utworz_logi();
    ustaw_env_ipc();
    
    log_info("Symulacja wystartuje za 2 sekundy...");
    sleep(2);
    
    printf("\n");
    printf(COLOR_GREEN COLOR_BOLD "╔═══════════════════════════════════════════╗\n");
    printf("║          SYMULACJA ROZPOCZĘTA!            ║\n");
    printf("╚═══════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    
    
    pid_t pid_kasjer = fork();
    if (pid_kasjer < 0) {
        perror("fork kasjer");
        exit(1);
    } else if (pid_kasjer == 0) {
        // Proces potomny
        execl("./bin/kasjer", "kasjer", NULL);
        perror("execl kasjer");
        exit(1);
    }
    dodaj_pid(pid_kasjer);
    log_success("Uruchomiono kasjera (PID %d)", pid_kasjer);
    sleep(1);
    
    pid_t pid_przewodnik1 = fork();
    if (pid_przewodnik1 < 0) {
        perror("fork przewodnik1");
        exit(1);
    } else if (pid_przewodnik1 == 0) {
        execl("./bin/przewodnik", "przewodnik", "1", NULL);
        perror("execl przewodnik1");
        exit(1);
    }
    dodaj_pid(pid_przewodnik1);
    log_success("Uruchomiono przewodnika 1 (PID %d)", pid_przewodnik1);
    sleep(1);
    
    pid_t pid_przewodnik2 = fork();
    if (pid_przewodnik2 < 0) {
        perror("fork przewodnik2");
        exit(1);
    } else if (pid_przewodnik2 == 0) {
        execl("./bin/przewodnik", "przewodnik", "2", NULL);
        perror("execl przewodnik2");
        exit(1);
    }
    dodaj_pid(pid_przewodnik2);
    log_success("Uruchomiono przewodnika 2 (PID %d)", pid_przewodnik2);
    sleep(1);
    
    pid_t pid_straznik = fork();
    if (pid_straznik < 0) {
        perror("fork straznik");
        exit(1);
    } else if (pid_straznik == 0) {
        execl("./bin/straznik", "straznik", NULL);
        perror("execl straznik");
        exit(1);
    }
    dodaj_pid(pid_straznik);
    log_success("Uruchomiono strażnika (PID %d)", pid_straznik);
    sleep(2);
    

    log_info("Uruchamiam %d zwiedzających...", LICZBA_ZWIEDZAJACYCH);
    
    for (int i = 0; i < LICZBA_ZWIEDZAJACYCH; i++) {
        // Losowe opóźnienie przed pojawieniem się
        sleep(losuj(OPOZNIENIE_ZWIEDZAJACY_MIN, OPOZNIENIE_ZWIEDZAJACY_MAX));
        
        pid_t pid_zwiedzajacy = fork();
        if (pid_zwiedzajacy < 0) {
            perror("fork zwiedzajacy");
            continue;
        } else if (pid_zwiedzajacy == 0) {
            execl("./bin/zwiedzajacy", "zwiedzajacy", NULL);
            perror("execl zwiedzajacy");
            exit(1);
        }
        
        dodaj_pid(pid_zwiedzajacy);
        log_info("Zwiedzający #%d (PID %d) pojawił się", i+1, pid_zwiedzajacy);
    }
    
    log_success("Wszyscy zwiedzający uruchomieni");
    
    // MONITOROWANIE    
    log_info("Symulacja trwa (max %d sekund)...", CZAS_SYMULACJI);
    
    // CZEKAJ NA PROCESY    
    log_info("Czas symulacji minął - czekam na procesy...");
    
    int status;
    pid_t pid;
    int zakonczonych = 0;
    
    while ((pid = wait(&status)) > 0) {
        zakonczonych++;
        
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                log_success("Proces %d zakończył się pomyślnie", pid);
            } else {
                log_warning("Proces %d zakończył się z kodem %d", pid, exit_code);
            }
        } else if (WIFSIGNALED(status)) {
            log_warning("Proces %d zakończony sygnałem %d", pid, WTERMSIG(status));
        }
    }
    
    log_info("Zakończono %d procesów potomnych", zakonczonych);
    
    
    printf("\n");
    printf(COLOR_YELLOW COLOR_BOLD "╔═══════════════════════════════════════════╗\n");
    printf("║        SYMULACJA ZAKOŃCZONA!              ║\n");
    printf("╚═══════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    
    printf(COLOR_BOLD "PODSUMOWANIE:\n" COLOR_RESET);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("Sprzedane bilety:     %d\n", stan_global->bilety_sprzedane);
    printf("  └─ Trasa 1:         %d\n", stan_global->bilety_trasa1);
    printf("  └─ Trasa 2:         %d\n", stan_global->bilety_trasa2);
    printf("Czas trwania:         %d sekund\n", CZAS_SYMULACJI);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("\n");
    
    printf(COLOR_GREEN "Logi zapisane w katalogu: logs/\n" COLOR_RESET);
    printf("  - bilety.txt       - sprzedane bilety\n");
    printf("  - trasa1.txt       - przewodnik 1\n");
    printf("  - trasa2.txt       - przewodnik 2\n");
    printf("  - symulacja.log    - ogólny log\n");
    printf("\n");
    
    log_success("Program zakończył się pomyślnie");
    
    return 0;
}
