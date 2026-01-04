#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include "../include/ipc.h"
#include "../include/utils.h"

// === PAMIĘĆ DZIELONA ===
int utworz_pamiec_dzielona(void) {
    int shmid = shmget(IPC_PRIVATE, sizeof(StanJaskini), IPC_CREAT | 0600);
    
    if (shmid < 0) {
        perror("shmget");
        log_error("Nie można utworzyć pamięci dzielonej");
        exit(1);
    }
    
    log_success("Utworzono pamięć dzieloną (ID: %d, rozmiar: %lu bajtów)", 
                shmid, sizeof(StanJaskini));
    
    return shmid;
}

StanJaskini* podlacz_pamiec_dzielona(int shmid) {
    StanJaskini *stan = (StanJaskini*) shmat(shmid, NULL, 0);
    
    if (stan == (StanJaskini*) -1) {
        perror("shmat");
        log_error("Nie można podłączyć pamięci dzielonej");
        exit(1);
    }
    
    return stan;
}

void odlacz_pamiec_dzielona(StanJaskini *stan) {
    if (shmdt(stan) < 0) {
        perror("shmdt");
        log_warning("Błąd odłączania pamięci dzielonej");
    }
}

void usun_pamiec_dzielona(int shmid) {
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        perror("shmctl IPC_RMID");
        log_warning("Błąd usuwania pamięci dzielonej");
    } else {
        log_success("Usunięto pamięć dzieloną");
    }
}

// === SEMAFORY ===
int utworz_semafory(void) {
    int semid = semget(IPC_PRIVATE, NUM_SEMS, IPC_CREAT | 0600);
    
    if (semid < 0) {
        perror("semget");
        log_error("Nie można utworzyć semaforów");
        exit(1);
    }
    
    log_success("Utworzono zestaw %d semaforów (ID: %d)", NUM_SEMS, semid);
    
    return semid;
}

void inicjalizuj_semafory(int semid) {
    union semun arg;
    
    // SEM_MUTEX - mutex (1)
    arg.val = 1;
    semctl(semid, SEM_MUTEX, SETVAL, arg);
    log_info("Semafor SEM_MUTEX = 1");
    
    // SEM_TRASA1_LIMIT - counting (N1)
    arg.val = N1;
    semctl(semid, SEM_TRASA1_LIMIT, SETVAL, arg);
    log_info("Semafor SEM_TRASA1_LIMIT = %d", N1);
    
    // SEM_TRASA2_LIMIT - counting (N2)
    arg.val = N2;
    semctl(semid, SEM_TRASA2_LIMIT, SETVAL, arg);
    log_info("Semafor SEM_TRASA2_LIMIT = %d", N2);
    
    // SEM_KLADKA1 - counting (K)
    arg.val = K;
    semctl(semid, SEM_KLADKA1, SETVAL, arg);
    log_info("Semafor SEM_KLADKA1 = %d", K);
    
    // SEM_KLADKA2 - counting (K)
    arg.val = K;
    semctl(semid, SEM_KLADKA2, SETVAL, arg);
    log_info("Semafor SEM_KLADKA2 = %d", K);
    
    // SEM_PRZEWODNIK1_READY - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_PRZEWODNIK1_READY, SETVAL, arg);
    log_info("Semafor SEM_PRZEWODNIK1_READY = 0");
    
    // SEM_PRZEWODNIK2_READY - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_PRZEWODNIK2_READY, SETVAL, arg);
    log_info("Semafor SEM_PRZEWODNIK2_READY = 0");

    arg.val = 0;
    semctl(semid, SEM_GRUPA1_WEJSCIE_KLADKA, SETVAL, arg);
    semctl(semid, SEM_GRUPA2_WEJSCIE_KLADKA, SETVAL, arg);
    semctl(semid, SEM_GRUPA1_WYJSCIE_KLADKA, SETVAL, arg);
    semctl(semid, SEM_GRUPA2_WYJSCIE_KLADKA, SETVAL, arg);
    semctl(semid, SEM_POTWIERDZENIE, SETVAL, arg);
    log_info("Semafory komunikacji zwiedzający-przewodnik = 0");
    
    log_success("Wszystkie semafory zainicjalizowane");
}

void sem_wait_safe(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    
    // Obsługa przerwania przez sygnał (EINTR) - ponawiaj operację
    while (semop(semid, &op, 1) < 0) {
        if (errno == EINTR) {
            // Przerwane przez sygnał - spróbuj ponownie
            continue; 
        } 
        // Inny błąd - raportuj ale nie kończ programu podczas zamykania
        perror("semop wait");
        log_error("Blad sem_wait na semaforze %d (errno=%d)", sem_num, errno);
        return;  // Zamiast exit(1) - pozwól kontynuować zamykanie
    }
}

void sem_signal_safe(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;
    
    while (semop(semid, &op, 1) < 0) {
        if (errno == EINTR) {
            continue; 
        }
        perror("semop signal");
        log_error("Blad sem_signal na semaforze %d (errno=%d)", sem_num, errno);
        return;
    }
}

int sem_getval_safe(int semid, int sem_num) {
    int val = semctl(semid, sem_num, GETVAL);
    if (val < 0) {
        perror("semctl GETVAL");
        return -1;
    }
    return val;
}

void usun_semafory(int semid) {
    if (semctl(semid, 0, IPC_RMID) < 0) {
        perror("semctl IPC_RMID");
        log_warning("Błąd usuwania semaforów");
    } else {
        log_success("Usunięto semafory");
    }
}

// === KOLEJKA KOMUNIKATÓW ===
int utworz_kolejke(void) {
    int msgid = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    
    if (msgid < 0) {
        perror("msgget");
        log_error("Nie można utworzyć kolejki komunikatów");
        exit(1);
    }
    
    log_success("Utworzono kolejkę komunikatów (ID: %d)", msgid);
    
    return msgid;
}

void usun_kolejke(int msgid) {
    if (msgctl(msgid, IPC_RMID, NULL) < 0) {
        perror("msgctl IPC_RMID");
        log_warning("Błąd usuwania kolejki komunikatów");
    } else {
        log_success("Usunięto kolejkę komunikatów");
    }
}

//  REJESTRACJA ZWIEDZAJĄCYCH (dla ewakuacji)
void zarejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    if (stan->liczba_aktywnych < MAX_ZWIEDZAJACYCH) {
        stan->zwiedzajacy_pids[stan->liczba_aktywnych] = pid;
        stan->liczba_aktywnych++;
        
        DEBUG_PRINT("Zarejestrowano zwiedzającego PID %d (łącznie: %d)", 
                    pid, stan->liczba_aktywnych);
    } else {
        log_warning("Przekroczono limit aktywnych zwiedzających!");
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
}

void wyrejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    int found = 0;
    for (int i = 0; i < stan->liczba_aktywnych; i++) {
        if (stan->zwiedzajacy_pids[i] == pid) {
            // Przesuń pozostałe PIDs
            for (int j = i; j < stan->liczba_aktywnych - 1; j++) {
                stan->zwiedzajacy_pids[j] = stan->zwiedzajacy_pids[j + 1];
            }
            stan->liczba_aktywnych--;
            found = 1;
            
            DEBUG_PRINT("Wyrejestrowano zwiedzającego PID %d (pozostało: %d)",
                        pid, stan->liczba_aktywnych);
            break;
        }
    }
    
    if (!found) {
        log_warning("Nie znaleziono PID %d do wyrejestrowania", pid);
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
}

void wypisz_stan_jaskini(StanJaskini *stan) {
    printf("\n");
    printf(COLOR_BOLD "╔═══════════════════════════════════════╗\n" COLOR_RESET);
    printf(COLOR_BOLD "║      STAN JASKINI (snapshot)          ║\n" COLOR_RESET);
    printf(COLOR_BOLD "╠═══════════════════════════════════════╣\n" COLOR_RESET);
    printf("║ Trasa 1: %2d/%2d osób                  ║\n", stan->trasa1_licznik, N1);
    printf("║ Trasa 2: %2d/%2d osób                  ║\n", stan->trasa2_licznik, N2);
    printf("║ Kładka 1: %2d/%2d (kier: %s)        ║\n", 
           stan->kladka1_licznik, K, 
           stan->kladka1_kierunek == 0 ? "WEJŚCIE" : "WYJŚCIE");
    printf("║ Kładka 2: %2d/%2d (kier: %s)        ║\n",
           stan->kladka2_licznik, K,
           stan->kladka2_kierunek == 0 ? "WEJŚCIE" : "WYJŚCIE");
    printf("╠═══════════════════════════════════════╣\n");
    printf("║ Bilety sprzedane: %3d                 ║\n", stan->bilety_sprzedane);
    printf("║   - Trasa 1: %3d                      ║\n", stan->bilety_trasa1);
    printf("║   - Trasa 2: %3d                      ║\n", stan->bilety_trasa2);
    printf(COLOR_BOLD "╚═══════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
}

void zapisz_log_symulacji(const char *format, ...) {
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    log_to_file(LOG_SYMULACJA, "%s", message);
}

