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

    arg.val = 0;
    semctl(semid, SEM_KOLEJKA1_NIEPUSTA, SETVAL, arg);
    semctl(semid, SEM_KOLEJKA2_NIEPUSTA, SETVAL, arg);
    log_info("Semafory kolejek niepustych = 0");
    
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
            continue; 
        } 
        perror("semop wait");
        log_error("Blad sem_wait na semaforze %d (errno=%d)", sem_num, errno);
        return;  
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
    
    // Najpierw sprawdź, czy ten PID już tu jest (ochrona przed duplikatami)
    for (int i = 0; i < MAX_ZWIEDZAJACYCH; i++) {
        if (stan->zwiedzajacy_pids[i] == pid) {
            sem_signal_safe(semid, SEM_MUTEX);
            return;
        }
    }

    int zarejestrowano = 0;
    for (int i = 0; i < MAX_ZWIEDZAJACYCH; i++) {
        if (stan->zwiedzajacy_pids[i] == 0) {
            stan->zwiedzajacy_pids[i] = pid;
            stan->liczba_aktywnych++; 
            zarejestrowano = 1;
            DEBUG_PRINT("Zarejestrowano zwiedzającego PID %d (aktywnych: %d)", 
                       pid, stan->liczba_aktywnych);
            break; 
        }
    }

    if (!zarejestrowano) {
        log_warning("Brak wolnych slotów dla zwiedzającego %d!", pid);
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
}

void wyrejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    int found = 0;
    for (int i = 0; i < MAX_ZWIEDZAJACYCH; i++) {
        if (stan->zwiedzajacy_pids[i] == pid) {
            stan->zwiedzajacy_pids[i] = 0;
            stan->liczba_aktywnych--;
            found = 1;
            DEBUG_PRINT("Wyrejestrowano zwiedzającego PID %d (pozostało: %d)", 
                       pid, stan->liczba_aktywnych);
            break; 
        }
    }
    
    if (!found) {
        DEBUG_PRINT("PID %d nie istnieje już w tablicy", pid);
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
}

//  KOLEJKOWANIE ZWIEDZAJĄCYCH 
void dolacz_do_kolejki(int trasa, pid_t pid, StanJaskini *stan, int semid) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    if (trasa == 1) {
        if (stan->kolejka_trasa1_koniec < MAX_ZWIEDZAJACYCH) {
            int idx = stan->kolejka_trasa1_koniec++;
            stan->kolejka_trasa1[idx] = pid;

            log_info("[KOLEJKA T1] Zwiedzający PID %d dołączył (pozycja %d)", pid, idx + 1);
        }
    } else {
        if (stan->kolejka_trasa2_koniec < MAX_ZWIEDZAJACYCH) {
            int idx = stan->kolejka_trasa2_koniec++;
            stan->kolejka_trasa2[idx] = pid;
            stan->para_flaga[idx] = 0;
            log_info("[KOLEJKA T2] Zwiedzający PID %d dołączył (pozycja %d)", pid, idx + 1);
        }
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
}

int zbierz_grupe(int nr_trasy, StanJaskini *stan, int semid, int max_osob) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    pid_t *kolejka = (nr_trasy == 1) ? stan->kolejka_trasa1 : stan->kolejka_trasa2;
    int *koniec = (nr_trasy == 1) ? &stan->kolejka_trasa1_koniec : &stan->kolejka_trasa2_koniec;
    pid_t *grupa_pids = (nr_trasy == 1) ? stan->grupa1_pids : stan->grupa2_pids;
    int *grupa_liczba = (nr_trasy == 1) ? &stan->grupa1_liczba : &stan->grupa2_liczba;
    
    if (*koniec == 0) {
        sem_signal_safe(semid, SEM_MUTEX);
        return 0;
    }
    
    int zebrano = 0;
    int idx = 0;
    
    while (idx < *koniec && zebrano < max_osob) {
        // Sprawdzaj flagę TYLKO dla trasy 2!
        if (nr_trasy == 2 && stan->para_flaga[idx] == 1) {
            // Para - weź oboje albo żadnego
            if (zebrano + 2 <= max_osob) {
                grupa_pids[zebrano++] = kolejka[idx++];  // Dziecko
                grupa_pids[zebrano++] = kolejka[idx++];  // Opiekun
                DEBUG_PRINT("[PRZEWODNIK %d] Zebrano parę dziecko+opiekun", nr_trasy);
            } else {
                DEBUG_PRINT("[PRZEWODNIK %d] Brak miejsca dla pary", nr_trasy);
                break;
            }
        } else {
            // Pojedyncza osoba (lub trasa 1 - nie ma par)
            grupa_pids[zebrano++] = kolejka[idx++];
        }
    }
    
    *grupa_liczba = zebrano;
    
    if (zebrano == 0) {
        sem_signal_safe(semid, SEM_MUTEX);
        return 0;
    }
    
    // Usuń z kolejki
    for (int i = idx; i < *koniec; i++) {
        kolejka[i - idx] = kolejka[i];
        // Kopiuj flagę TYLKO dla trasy 2
        if (nr_trasy == 2) {
            stan->para_flaga[i - idx] = stan->para_flaga[i];
        }
    }
    *koniec -= idx;
    
    log_success("[PRZEWODNIK %d] Zebrał grupę %d osób (w kolejce pozostało: %d)",
               nr_trasy, zebrano, *koniec);
    
    sem_signal_safe(semid, SEM_MUTEX);
    return zebrano;
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

