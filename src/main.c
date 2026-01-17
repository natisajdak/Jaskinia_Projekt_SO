#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Globalne ID struktur IPC
int shmid_global = -1;
int semid_global = -1;
int msgid_global = -1;
StanJaskini *stan_global = NULL;

volatile sig_atomic_t zamkniecie = 0;

// Lista PID procesów potomnych
#define MAX_PIDS 200
pid_t pids[MAX_PIDS];
int liczba_procesow = 0;

void cleanup(void) {
    log_info("Sprzątanie zasobów...");
    
    if (stan_global) {
        wypisz_stan_jaskini(stan_global);
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

void obsluga_sigint(int sig) {
    (void)sig;
    zamkniecie = 1;
}

void awaryjne_zamkniecie(void) {
    printf("\n");
    log_warning("╔═══════════════════════════════════════╗");
    log_warning("║    AWARYJNE ZAMYKANIE JASKINI...      ║");
    log_warning("╚═══════════════════════════════════════╝");
    
    if (stan_global) {
        sem_wait_safe(semid_global, SEM_MUTEX);
        stan_global->zamkniecie_przewodnik1 = 1;
        stan_global->zamkniecie_przewodnik2 = 1;
        stan_global->jaskinia_otwarta = 0;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_warning("→ Jaskinia oznaczona jako ZAMKNIĘTA");
        
        if (stan_global->pid_przewodnik1 > 0) {
            log_info("→ Wysyłam SIGUSR1 do przewodnika 1 (PID %d)", stan_global->pid_przewodnik1);
            if (kill(stan_global->pid_przewodnik1, SIGUSR1) == -1) {
                perror("kill przewodnik1");
            }
        }
        
        if (stan_global->pid_przewodnik2 > 0) {
            log_info("→ Wysyłam SIGUSR2 do przewodnika 2 (PID %d)", stan_global->pid_przewodnik2);
            if (kill(stan_global->pid_przewodnik2, SIGUSR2) == -1) {
                perror("kill przewodnik2");
            }
        }
        
        if (stan_global->pid_generator > 0) {
            log_info("→ Wysyłam SIGTERM do generatora (PID %d)", stan_global->pid_generator);
            kill(stan_global->pid_generator, SIGTERM);
        }
        
        log_warning("→ Ewakuacja aktywnych zwiedzających...");
        
        int ewakuowani = 0;
        for (int i = 0; i < MAX_ZWIEDZAJACYCH_TABLICA; i++) {
            if (stan_global->zwiedzajacy_pids[i] > 0) {
                kill(stan_global->zwiedzajacy_pids[i], SIGTERM);
                ewakuowani++;
            }
        }
        
        log_success("→ Wysłano sygnały ewakuacji do %d zwiedzających", ewakuowani);
    }

    log_info("→ Zakańczam procesy potomne...");
    for (int i = 0; i < liczba_procesow; i++) {
        if (pids[i] > 0) {
            if (kill(pids[i], SIGTERM) == -1 && errno != ESRCH) {
                perror("kill TERM");
            }
        }
    }
    
    sleep(2);
    
    log_warning("→ Wymuszam zakończenie pozostałych procesów...");
    for (int i = 0; i < liczba_procesow; i++) {
        if (pids[i] > 0) {
            if (kill(pids[i], SIGKILL) == -1 && errno != ESRCH) {
                perror("kill KILL");
            }
        }
    }
    
    sleep(1);
    log_info("→ Zbieram procesy zombie...");
    int status;
    pid_t pid;
    int zebranych = 0;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        zebranych++;
        DEBUG_PRINT("Zebrany proces %d", pid);
    }
    
    if (zebranych > 0) {
        log_info("→ Zebrano %d procesów", zebranych);
    }
    
    log_success("AWARYJNE ZAMKNIĘCIE ZAKOŃCZONE");
}


void waliduj_parametry(void) {
    int bledy = 0;
    
    if (N1 <= K) {
        log_error("BŁĄD: N1 (%d) musi być > K (%d)", N1, K);
        bledy++;
    }
    if (N2 <= K) {
        log_error("BŁĄD: N2 (%d) musi być > K (%d)", N2, K);
        bledy++;
    }
    if (TP >= TK) {
        log_error("BŁĄD: Tp (%d:00) musi być < Tk (%d:00)", TP, TK);
        bledy++;
    }
    if (K < 1) {
        log_error("BŁĄD: K musi być >= 1");
        bledy++;
    }
    
    if (bledy > 0) {
        log_error("Wykryto %d błędów w parametrach - zakończenie", bledy);
        exit(1);
    }
    
    log_success("Parametry zwalidowane: N1=%d, N2=%d, K=%d, T1=%d, T2=%d, Tp=%d:00, Tk=%d:00",
                N1, N2, K, T1, T2, TP, TK);
}

void inicjalizuj_ipc(void) {
    log_info("Inicjalizacja struktur IPC...");
    
    shmid_global = utworz_pamiec_dzielona();
    stan_global = podlacz_pamiec_dzielona(shmid_global);
    
    memset(stan_global, 0, sizeof(StanJaskini));
    stan_global->pid_main = getpid();
    stan_global->jaskinia_otwarta = 0;
    
    log_success("Pamięć dzielona zainicjalizowana");
    
    semid_global = utworz_semafory();
    inicjalizuj_semafory(semid_global);
    
    msgid_global = utworz_kolejke();
    
    log_success("Wszystkie struktury IPC gotowe");
}

void utworz_logi(void) {
    if (system("mkdir -p logs") != 0) {
        perror("system mkdir");
        log_warning("Nie można utworzyć katalogu logs/");
    }
    
    unlink(LOG_BILETY);
    unlink(LOG_TRASA1);
    unlink(LOG_TRASA2);
    unlink(LOG_SYMULACJA);
    unlink(LOG_GENERATOR); 
    
    time_t now = time(NULL);
    log_to_file(LOG_SYMULACJA, "=== START SYMULACJI JASKINIA ===");
    log_to_file(LOG_SYMULACJA, "Data: %s", ctime(&now));
    log_to_file(LOG_SYMULACJA, "Parametry: N1=%d, N2=%d, K=%d, T1=%d, T2=%d",
                N1, N2, K, T1, T2);
    
    log_to_file(LOG_BILETY, "PID | Wiek | Trasa | Powrót | Cena | Status | Info");
    log_to_file(LOG_TRASA1, "=== LOGI PRZEWODNIKA TRASY 1 ===");
    log_to_file(LOG_TRASA2, "=== LOGI PRZEWODNIKA TRASY 2 ===");
    log_to_file(LOG_GENERATOR, "=== LOG GENERATORA ZWIEDZAJĄCYCH ===");
    
    log_success("Pliki logów utworzone");
}

void ustaw_env_ipc(void) {
    char buf[32];
    
    snprintf(buf, sizeof(buf), "%d", shmid_global);
    setenv("SHMID", buf, 1);
    
    snprintf(buf, sizeof(buf), "%d", semid_global);
    setenv("SEMID", buf, 1);
    
    snprintf(buf, sizeof(buf), "%d", msgid_global);
    setenv("MSGID", buf, 1);
}

void dodaj_pid(pid_t pid) {
    if (liczba_procesow < MAX_PIDS) {
        pids[liczba_procesow++] = pid;
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("\n");
    printf(COLOR_BOLD COLOR_CYAN);
    printf("╔═══════════════════════════════════════╗\n");
    printf("║                 JASKINIA              ║\n");
    printf("╚═══════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("\n");
    
    srand(time(NULL));
    
    atexit(cleanup);
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = obsluga_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    waliduj_parametry();
    inicjalizuj_ipc();
    utworz_logi();
    ustaw_env_ipc();
    
    // WYŚWIETL INFORMACJE
    printf(COLOR_CYAN "\n");
    printf("╔═════════════════════════════════════════════╗\n");
    printf("║      PARAMETRY CZASU SYMULACJI              ║\n");
    printf("╠═════════════════════════════════════════════╣\n");
    printf("║ Godziny jaskini:     %02d:00 - %02d:00          ║\n", TP, TK);
    printf("║ Czas rzeczywisty:    %d godzin              ║\n", TK - TP);
    printf("║ Przyspieszenie:      %dx                   ║\n", PRZYSPIESZENIE);
    printf("║ Czas symulacji:      %d sek (~%.1f min)      ║\n",
           CZAS_OTWARCIA_SEK, CZAS_OTWARCIA_SEK / 60.0);
    printf("╠═════════════════════════════════════════════╣\n");
    printf("║ 1 sekunda symulacji = %.1f min rzeczywistych║\n",
           PRZYSPIESZENIE / 60.0);
    printf("╚═════════════════════════════════════════════╝\n");
    printf(COLOR_RESET "\n");
    
    // OTWÓRZ JASKINIĘ
    sem_wait_safe(semid_global, SEM_MUTEX);
    stan_global->jaskinia_otwarta = 1;
    stan_global->czas_startu = time(NULL);
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    log_success("JASKINIA OTWARTA! (godzina %02d:00, zamknięcie %02d:00)", TP, TK);
    
    printf("\n");
    printf(COLOR_GREEN COLOR_BOLD "╔═══════════════════════════════════════╗\n");
    printf("║          SYMULACJA ROZPOCZĘTA!        ║\n");
    printf("╚═══════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    
    // URUCHOM PROCESY
    pid_t pid_kasjer = fork();
    if (pid_kasjer == 0) {
        execl("./bin/kasjer", "kasjer", NULL);
        perror("execl kasjer");
        _exit(1);
    }
    dodaj_pid(pid_kasjer);
    sleep(1);
    if (kill(pid_kasjer, 0) != 0) {
        log_error("Nie udało się uruchomić kasjera (PID %d)", pid_kasjer);
        exit(1);
    }

    log_success("Uruchomiono kasjera (PID %d)", pid_kasjer);
    sleep(1);
    
    pid_t pid_przewodnik1 = fork();
    if (pid_przewodnik1 == 0) {
        execl("./bin/przewodnik", "przewodnik", "1", NULL);
        perror("execl przewodnik1");
        exit(1);
    }
    dodaj_pid(pid_przewodnik1);
    log_success("Uruchomiono przewodnika 1 (PID %d)", pid_przewodnik1);
    sleep(1);
    
    pid_t pid_przewodnik2 = fork();
    if (pid_przewodnik2 == 0) {
        execl("./bin/przewodnik", "przewodnik", "2", NULL);
        perror("execl przewodnik2");
        exit(1);
    }
    dodaj_pid(pid_przewodnik2);
    log_success("Uruchomiono przewodnika 2 (PID %d)", pid_przewodnik2);
    sleep(1);
    
    pid_t pid_straznik = fork();
    if (pid_straznik == 0) {
        execl("./bin/straznik", "straznik", NULL);
        perror("execl straznik");
        exit(1);
    }
    dodaj_pid(pid_straznik);
    log_success("Uruchomiono strażnika (PID %d)", pid_straznik);
    sleep(2);
    
    // POPRAWKA 1: Uruchom GENERATOR (zamiast pętli for)
    pid_t pid_generator = fork();
    if (pid_generator == 0) {
        execl("./bin/generator", "generator", NULL);
        perror("execl generator");
        exit(1);
    }
    dodaj_pid(pid_generator);
    log_success("Uruchomiono generator zwiedzających (PID %d)", pid_generator);
    
    log_info("Generator będzie tworzył zwiedzających LOSOWO przez %d sekund", CZAS_OTWARCIA_SEK);
    log_info("Częstotliwość: co %d-%d sekund", GENERATOR_MIN_DELAY, GENERATOR_MAX_DELAY);
    
    // CZEKAJ NA PROCESY
    int status;
    pid_t pid;
    int zakonczonych = 0;
    
    time_t start_wait = time(NULL);
    int max_wait_time = CZAS_SYMULACJI + 30;
    
    while (1) {
        if (zamkniecie) {
            log_warning("Przerwanie przez użytkownika - kończę czekanie");
            awaryjne_zamkniecie();
            break;
        }
        
        pid = waitpid(-1, &status, WNOHANG);
        
        if (pid > 0) {
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
        } else if (pid == 0) {
            if (difftime(time(NULL), start_wait) > max_wait_time) {
                log_warning("Timeout oczekiwania na procesy - wymuszam zakończenie");
                
                for (int i = 0; i < liczba_procesow; i++) {
                    if (pids[i] > 0) {
                        kill(pids[i], SIGKILL);
                    }
                }
                sleep(1);
                break;
            }
            sleep(1);
        } else {
            if (errno == ECHILD) {
                log_success("Brak procesów potomnych - wszystkie zakończone");
                break;
            } else {
                perror("waitpid");
                break;
            }
        }
    }
    
    log_info("Sprawdzam pozostałe procesy...");
    while ((pid = wait(&status)) > 0) {
        zakonczonych++;
        log_info("Zebrany proces zombie: %d", pid);
    }
    
    log_info("Zakończono %d procesów potomnych", zakonczonych);
    
    printf("\n");
    printf(COLOR_YELLOW COLOR_BOLD "╔═══════════════════════════════════════╗\n");
    printf("║        SYMULACJA ZAKOŃCZONA!          ║\n");
    printf("╚═══════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    
    printf(COLOR_BOLD "PODSUMOWANIE:\n" COLOR_RESET);
    printf("─────────────────────────────────────────────\n");
    printf("Wygenerowano:         %d zwiedzających\n", stan_global->licznik_wygenerowanych);
    printf("Sprzedane bilety:     %d\n", stan_global->bilety_sprzedane);
    printf("  ├─ Trasa 1:         %d\n", stan_global->bilety_trasa1);
    printf("  ├─ Trasa 2:         %d\n", stan_global->bilety_trasa2);
    printf("  └─ Powtórki:        %d\n", stan_global->bilety_powrot);
    printf("Odmowy:               %d\n", stan_global->licznik_odrzuconych);
    printf("Czas trwania:         %d sekund\n", CZAS_SYMULACJI);
    printf("─────────────────────────────────────────────\n");
    printf("\n");
    
    printf(COLOR_GREEN "Logi zapisane w katalogu: logs/\n" COLOR_RESET);
    printf("  - bilety.txt       - sprzedane bilety\n");
    printf("  - trasa1.txt       - przewodnik 1\n");
    printf("  - trasa2.txt       - przewodnik 2\n");
    printf("  - generator.txt    - wygenerowane osoby\n");
    printf("  - symulacja.log    - ogólny log\n");
    printf("\n");
    
    log_success("Program zakończył się pomyślnie");
    
    return 0;
}