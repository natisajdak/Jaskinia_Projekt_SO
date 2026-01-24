#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <time.h>
#include "../include/ipc.h"
#include "../include/utils.h"

volatile sig_atomic_t flaga_stop_ipc = 0;

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
    
    log_info("═══ INICJALIZACJA SEMAFORÓW ═══");
    
    // SEM_MUTEX - mutex (1)
    arg.val = 1;
    semctl(semid, SEM_MUTEX, SETVAL, arg);
    log_info("Semafor SEM_MUTEX = 1");

    arg.val = 1;
    semctl(semid, SEM_MUTEX_KLADKA, SETVAL, arg);
    log_info("Semafor SEM_MUTEX_KLADKA = 1");
    
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
    
    // SEM_KOLEJKA1_NIEPUSTA - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_KOLEJKA1_NIEPUSTA, SETVAL, arg);
    log_info("Semafor SEM_KOLEJKA1_NIEPUSTA = 0");
    
    // SEM_KOLEJKA2_NIEPUSTA - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_KOLEJKA2_NIEPUSTA, SETVAL, arg);
    log_info("Semafor SEM_KOLEJKA2_NIEPUSTA = 0");
    
    // SEM_PRZEWODNIK1_READY - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_PRZEWODNIK1_READY, SETVAL, arg);
    log_info("Semafor SEM_PRZEWODNIK1_READY = 0");
    
    // SEM_PRZEWODNIK2_READY - binary (0 na start)
    arg.val = 0;
    semctl(semid, SEM_PRZEWODNIK2_READY, SETVAL, arg);
    log_info("Semafor SEM_PRZEWODNIK2_READY = 0");
    
    // SEM_KLADKA1_WEJSCIE_ALLOWED - binary (0 = zamknięte na start)
    arg.val = 0;
    semctl(semid, SEM_KLADKA1_WEJSCIE_ALLOWED, SETVAL, arg);
    log_info("Semafor SEM_KLADKA1_WEJSCIE_ALLOWED = 0 (zamknięte)");
    
    // SEM_KLADKA1_WYJSCIE_ALLOWED - binary (0 = zamknięte na start)
    arg.val = 0;
    semctl(semid, SEM_KLADKA1_WYJSCIE_ALLOWED, SETVAL, arg);
    log_info("Semafor SEM_KLADKA1_WYJSCIE_ALLOWED = 0 (zamknięte)");
    
    // SEM_KLADKA2_WEJSCIE_ALLOWED - binary (0 = zamknięte na start)
    arg.val = 0;
    semctl(semid, SEM_KLADKA2_WEJSCIE_ALLOWED, SETVAL, arg);
    log_info("Semafor SEM_KLADKA2_WEJSCIE_ALLOWED = 0 (zamknięte)");
    
    // SEM_KLADKA2_WYJSCIE_ALLOWED - binary (0 = zamknięte na start)
    arg.val = 0;
    semctl(semid, SEM_KLADKA2_WYJSCIE_ALLOWED, SETVAL, arg);
    log_info("Semafor SEM_KLADKA2_WYJSCIE_ALLOWED = 0 (zamknięte)");
    
    // SEM_KOLEJKA_MSG_SLOTS - counting (98 = 100 - 2 odpowiedzi)
    arg.val = MAX_MSG_QUEUE - 2;
    semctl(semid, SEM_KOLEJKA_MSG_SLOTS, SETVAL, arg);
    
    // SEM_POTWIERDZENIE - counting (0 na start)
    arg.val = 0;
    semctl(semid, SEM_POTWIERDZENIE_WEJSCIE_TRASA1, SETVAL, arg);
    log_info("Semafor SEM_POTWIERDZENIE_WEJSCIE_TRASA1 = 0");

    arg.val = 0;
    semctl(semid, SEM_POTWIERDZENIE_WYJSCIE_TRASA1, SETVAL, arg);
    log_info("Semafor SEM_POTWIERDZENIE_WYJSCIE_TRASA1 = 0 ");

    arg.val = 0;
    semctl(semid, SEM_POTWIERDZENIE_WEJSCIE_TRASA2, SETVAL, arg);
    log_info("Semafor SEM_POTWIERDZENIE_WEJSCIE_TRASA2 = 0");

    arg.val = 0;
    semctl(semid, SEM_POTWIERDZENIE_WYJSCIE_TRASA2, SETVAL, arg);
    log_info("Semafor SEM_POTWIERDZENIE_WYJSCIE_TRASA2 = 0 ");

    arg.val = 0;
    semctl(semid, SEM_ZAKONCZENI, SETVAL, arg);

    semctl(semid, SEM_WOLNE_SLOTY_ZWIEDZAJACYCH, SETVAL, MAX_ZWIEDZAJACYCH_TABLICA);
    
    log_success("Wszystkie semafory zainicjalizowane (%d semaforów)", NUM_SEMS);
}

void sem_wait_safe(int semid, int sem_num) {
    struct sembuf op = {sem_num, -1, 0};
    
    while (semop(semid, &op, 1) < 0) {
        if (errno == EINTR) {
            if (flaga_stop_ipc) {
                _exit(0); 
            }
            continue;
        }
        if (errno == EIDRM || errno == EINVAL) _exit(0);
        perror("semop fatal error");
        _exit(1);
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
        if (errno == EIDRM || errno == EINVAL) {
            DEBUG_PRINT("Semafory usunięte - kończę operację na sem %d", sem_num);
            exit(0);
        }
        perror("semop signal");
        log_error("Blad sem_signal na semaforze %d (errno=%d)", sem_num, errno);
        return;
    }
}

int sem_trywait_safe(int semid, int sem_num) {
    struct sembuf op = {sem_num, -1, IPC_NOWAIT};
    if (semop(semid, &op, 1) < 0) {
        if (errno == EAGAIN) return 0; 
        if (errno == EIDRM) _exit(0);
    }
    return 1; 
}

int sem_getval_safe(int semid, int sem_num) {
    int val = semctl(semid, sem_num, GETVAL);
    if (val < 0) {
        perror("semctl GETVAL");
        return -1;
    }
    return val;
}

int sem_timed_wait_safe(int semid, int sem_num, int timeout_sec) {
    time_t deadline = time(NULL) + timeout_sec;
    
    while (1) {
        struct sembuf op_try = {sem_num, -1, IPC_NOWAIT};
        
        if (semop(semid, &op_try, 1) == 0) {
            return 0;
        }
        
        if (errno == EAGAIN) {
            if (time(NULL) >= deadline) {
                return -1;  
            }
            usleep(10000);
            continue;
        }
        
        if (errno == EINTR) continue;
        if (errno == EIDRM || errno == EINVAL) return -2;
        
        return -2;
    }
}

void usun_semafory(int semid) {
    if (semctl(semid, 0, IPC_RMID) < 0) {
        perror("semctl IPC_RMID");
        log_warning("Błąd usuwania semaforów");
    } else {
        log_success("Usunięto semafory");
    }
}

void ustaw_semafor_na_zero(int semid, int sem) {
    int val;
    do {
        val = semctl(semid, sem, GETVAL);
        if (val > 0) {
            sem_wait_safe(semid, sem);
        }
    } while (val > 0);
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

// Sprawdzenie liczby komunikatów w kolejce
int sprawdz_miejsce_w_kolejce(int msgid) {
    struct msqid_ds buf;
    if (msgctl(msgid, IPC_STAT, &buf) < 0) {
        perror("msgctl IPC_STAT");
        return -1;
    }
    return (int)buf.msg_qnum;  // Liczba komunikatów w kolejce
}

// === REJESTRACJA ZWIEDZAJĄCYCH ===
int zarejestruj_zwiedzajacego(StanJaskini *stan, pid_t pid, int semid, int czy_opiekun) {
    sem_wait_safe(semid, SEM_WOLNE_SLOTY_ZWIEDZAJACYCH);
    sem_wait_safe(semid, SEM_MUTEX);
    int znaleziony_indeks = -1;

    for (int i = 0; i < MAX_ZWIEDZAJACYCH_TABLICA; i++) {
        if (stan->zwiedzajacy_pids[i] == 0) {
            stan->zwiedzajacy_pids[i] = pid;
            stan->zwiedzajacy_jest_opiekunem[i] = czy_opiekun; 
            stan->liczba_aktywnych++;
            znaleziony_indeks = i;
            DEBUG_PRINT("Zarejestrowano zwiedzającego PID %d (Opiekun: %d)", pid, czy_opiekun);
            break;
        }
    }

    if (znaleziony_indeks == -1) {
        log_error("Brak wolnych slotów dla zwiedzającego w pamięci dzielonej!");
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
    return znaleziony_indeks;
}

void wyrejestruj_zwiedzajacego(StanJaskini *stan, int indeks, int semid) {
    if (indeks < 0 || indeks >= MAX_ZWIEDZAJACYCH_TABLICA) return;

    sem_wait_safe(semid, SEM_MUTEX);
    
    if (stan->zwiedzajacy_pids[indeks] != 0) {
        stan->zwiedzajacy_pids[indeks] = 0;
        stan->zwiedzajacy_jest_opiekunem[indeks] = 0; 
        stan->liczba_aktywnych--;
        DEBUG_PRINT("Zwiedzający wyrejestrowany. Slot %d zwolniony (pozostało aktywnych: %d)", 
                    indeks, stan->liczba_aktywnych);
    }
    
    sem_signal_safe(semid, SEM_MUTEX);
    sem_signal_safe(semid, SEM_WOLNE_SLOTY_ZWIEDZAJACYCH);
}

void dolacz_do_kolejki(int nr_trasy, pid_t pid, StanJaskini *stan, int semid) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    pid_t *kolejka = (nr_trasy == 1) ? 
        stan->kolejka_trasa1 : stan->kolejka_trasa2;
    int *koniec = (nr_trasy == 1) ? 
        &stan->kolejka_trasa1_koniec : &stan->kolejka_trasa2_koniec;
    
    kolejka[*koniec] = pid;
    (*koniec)++;
    
    DEBUG_PRINT("Zwiedzający PID %d dołączył do kolejki trasy %d (w kolejce: %d)",
                pid, nr_trasy, *koniec);
    
    int sem_kolejka = (nr_trasy == 1) ? 
        SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
    sem_signal_safe(semid, sem_kolejka);
    
    sem_signal_safe(semid, SEM_MUTEX);
}

int zbierz_grupe(int nr_trasy, StanJaskini *stan, int semid, int max_osob) {
    sem_wait_safe(semid, SEM_MUTEX);
    
    pid_t *kolejka = (nr_trasy == 1) ? stan->kolejka_trasa1 : stan->kolejka_trasa2;
    int *koniec = (nr_trasy == 1) ? &stan->kolejka_trasa1_koniec : &stan->kolejka_trasa2_koniec;
    pid_t *grupa_pids = (nr_trasy == 1) ? stan->grupa1_pids : stan->grupa2_pids;
    int *grupa_liczba = (nr_trasy == 1) ? &stan->grupa1_liczba : &stan->grupa2_liczba;
    
    if (*koniec == 0) {
        int sem_kolejka = (nr_trasy == 1) ? 
            SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
        
        // Wyczyść semafor (może być > 1 jeśli było wiele zwiedzających)
        while (sem_trywait_safe(semid, sem_kolejka) == 1) {
            // Dekrementuj aż do 0
        }
        
        log_info("[PRZEWODNIK %d] Kolejka pusta - wyzerowano semafor NIEPUSTA", nr_trasy);
    }
    
    int zebrano_osob = 0;      // Liczba miejsc (opiekun+dziecko = 2)
    int zebrano_procesow = 0;  // Liczba procesów
    int idx = 0;
    
    // Zbierz procesy z kolejki
    while (idx < *koniec && zebrano_osob < max_osob) {
        pid_t pid = kolejka[idx];
        
        // Sprawdź czy proces istnieje
        if (kill(pid, 0) != 0) {
            log_warning("[PRZEWODNIK %d] PID %d nie istnieje - pomijam", nr_trasy, pid);
            for (int i = idx; i < *koniec - 1; i++) {
                kolejka[i] = kolejka[i + 1];
            }
            (*koniec)--;
            continue;
        }
        
        // Sprawdź czy to opiekun
        int real_idx = -1;
        for (int k = 0; k < MAX_ZWIEDZAJACYCH_TABLICA; k++) {
            if (stan->zwiedzajacy_pids[k] == pid) {
                real_idx = k;
                break;
            }
        }

        int czy_opiekun = 0;
        if (real_idx != -1) {
            czy_opiekun = stan->zwiedzajacy_jest_opiekunem[real_idx];
        } else {
            break; 
        }

        int liczba_miejsc = czy_opiekun ? 2 : 1;
        
        // Sprawdź czy jest miejsce
        if (zebrano_osob + liczba_miejsc > max_osob) {
            break;
        }
        
        // Dodaj do grupy
        grupa_pids[zebrano_procesow] = pid;
        zebrano_procesow++;
        zebrano_osob += liczba_miejsc;

        log_info("[PRZEWODNIK %d] Zwiedzający PID=%d %s zajął %d miejsc", nr_trasy, pid, czy_opiekun ? "(OPIEKUN)" : "(ZWYKŁY)",
                 liczba_miejsc);
        
        idx++;
    }
    
    *grupa_liczba = zebrano_osob;
    
    // Usuń zebrane procesy z kolejki (przesuń pozostałe na początek)
    for (int i = 0; i < *koniec - idx; i++) {
        kolejka[i] = kolejka[i + idx];
    }
    *koniec -= idx;
    
    log_success("[PRZEWODNIK %d] Zebrał grupę %d osób (%d procesów, w kolejce pozostało: %d)",
               nr_trasy, zebrano_osob, zebrano_procesow, *koniec);
    
    sem_signal_safe(semid, SEM_MUTEX);
    return zebrano_procesow;
}

void wypisz_stan_jaskini(StanJaskini *stan) {
    printf("\n" COLOR_BOLD COLOR_CYAN);
    printf(COLOR_BOLD "╔═══════════════════════════════════════╗\n" COLOR_CYAN);
    printf(COLOR_BOLD "║      STAN JASKINI                     ║\n" COLOR_CYAN);
    printf(COLOR_BOLD "╠═══════════════════════════════════════╣\n" COLOR_CYAN);
    printf("║ Trasa 1: %2d/%2d osób                   ║\n", stan->trasa1_licznik, N1);
    printf("║ Trasa 2: %2d/%2d osób                   ║\n", stan->trasa2_licznik, N2);
    printf("║ Kładka 1: %2d/%2d (kier: %s)       ║\n", 
           stan->kladka1_licznik, K, 
           stan->kladka1_kierunek == 0 ? "WEJŚCIE" : "WYJŚCIE");
    printf("║ Kładka 2: %2d/%2d (kier: %s)       ║\n",
           stan->kladka2_licznik, K,
           stan->kladka2_kierunek == 0 ? "WEJŚCIE" : "WYJŚCIE");
    printf("╠═══════════════════════════════════════╣\n");
    printf("║ Bilety sprzedane: %3d                 ║\n", stan->bilety_sprzedane);
    printf("║   - Trasa 1: %3d                      ║\n", stan->bilety_trasa1);
    printf("║   - Trasa 2: %3d                      ║\n", stan->bilety_trasa2);
    printf("║ W kolejkach:                          ║\n");
    printf("║   - Trasa 1: %3d osób                 ║\n", stan->kolejka_trasa1_koniec);
    printf("║   - Trasa 2: %3d osób                 ║\n", stan->kolejka_trasa2_koniec);
    printf(COLOR_BOLD "╚═══════════════════════════════════════╝\n" COLOR_CYAN);
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