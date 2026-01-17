#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

volatile sig_atomic_t flaga_zamkniecie = 0;
void obsluga_sigterm(int sig) {
    (void)sig;
    flaga_zamkniecie = 1;
}

// Walidacja biletu (z obsługą opiekuna)
int waliduj_bilet(MsgBilet *prosba, char *powod, size_t powod_size, StanJaskini *stan, int semid) {
    // 1. Opiekun z dzieckiem - zawsze trasa 2
    if (prosba->jest_opiekunem && prosba->trasa != 2) {
        snprintf(powod, powod_size, "Opiekun z dzieckiem może zwiedzać tylko trasę 2");
        return 0;
    }
    
    // 2. Dzieci <8 lat bez opiekuna - ODMOWA
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO && !prosba->jest_opiekunem) {
        snprintf(powod, powod_size,
                 "Dzieci poniżej %d lat wymagają opiekuna (osoby dorosłej ≥%d lat)",
                 WIEK_TYLKO_TRASA2_DZIECKO, WIEK_DOROSLY);
        
        sem_wait_safe(semid, SEM_MUTEX);
        stan->bilety_dzieci_bez_opieki++;
        sem_signal_safe(semid, SEM_MUTEX);
        
        return 0;
    }
    
    // 3. Dzieci <8 lat: tylko trasa 2 (jeśli samodzielne - ale wcześniej odrzucone)
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO && prosba->trasa != 2) {
        snprintf(powod, powod_size,
                 "Dzieci poniżej %d lat mogą zwiedzać tylko trasę 2",
                 WIEK_TYLKO_TRASA2_DZIECKO);
        return 0;
    }
    
    // 4. Seniorzy >75 lat: tylko trasa 2
    if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR && prosba->trasa != 2) {
        snprintf(powod, powod_size,
                 "Osoby powyżej %d lat mogą zwiedzać tylko trasę 2",
                 WIEK_TYLKO_TRASA2_SENIOR);
        return 0;
    }
    
    return 1; // OK
}

// Cena biletu 
float oblicz_cene_biletu(MsgBilet *prosba) {
    float cena_opiekun = 0.0f;
    float cena_dziecko = 0.0f;
    
    // 1. Cena opiekuna
    if (prosba->wiek < WIEK_GRATIS) {
        cena_opiekun = 0.0f;
    } else if (prosba->czy_powrot) {
        cena_opiekun = CENA_BAZOWA * ZNIZKA_POWROT;
    } else if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR) {
        cena_opiekun = CENA_BAZOWA * 0.7f;  // Senior 30% zniżki
    } else {
        cena_opiekun = CENA_BAZOWA;
    }
    
    // 2. Cena dziecka (jeśli opiekun)
    if (prosba->jest_opiekunem) {
        if (prosba->wiek_dziecka < WIEK_GRATIS) {
            cena_dziecko = 0.0f;
        } else {
            // Dziecko z opiekunem: 20% zniżki
            cena_dziecko = CENA_BAZOWA * (1.0f - ZNIZKA_DZIECKO_Z_OPIEKUNEM);
        }
    }
    
    return cena_opiekun + cena_dziecko;
}

const char* okresl_typ_biletu(MsgBilet *prosba) {
    if (prosba->czy_powrot) {
        return "POWTÓRKA";
    }
    
    if (prosba->jest_opiekunem) {
        return "OPIEKUN_Z_DZIECKIEM";
    }
    
    if (prosba->wiek < WIEK_GRATIS) {
        return "DZIECKO_GRATIS";
    }
    
    if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR) {
        return "SENIOR";
    }
    
    return "NORMALNY";
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    log_info("[KASJER] Start (PID: %d)", getpid());
    
    int shmid = atoi(getenv("SHMID"));
    int semid = atoi(getenv("SEMID"));
    int msgid = atoi(getenv("MSGID"));
    
    StanJaskini *stan = podlacz_pamiec_dzielona(shmid);
    stan->pid_kasjer = getpid();
    
    log_success("[KASJER] Połączono z IPC");
    
    // POPRAWKA 7: JEDNOWĄTKOWY kasjer (usunięto pthread)
    int bilety_lokalne = 0;
    
    log_info("[KASJER] Czekam na klientów...");
    
    signal(SIGTERM, obsluga_sigterm);
    while (stan->jaskinia_otwarta && !flaga_zamkniecie) {
        int w_kolejce = sprawdz_miejsce_w_kolejce(msgid);
        if (w_kolejce >= MAX_MSG_QUEUE - 5) {
            log_info("[KASJER] Kolejka prawie pełna (%d/%d) - czekam w_kolejce", w_kolejce, MAX_MSG_QUEUE);
            sleep(1);
        } 

        MsgBilet prosba;
        
        // BLOKUJĄCE msgrcv 
        ssize_t ret = msgrcv(msgid, &prosba, sizeof(MsgBilet) - sizeof(long), -2, 0); 
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;  // Przerwany przez sygnał
            } else if (errno == EIDRM) {
                log_info("[KASJER] Kolejka usunięta - kończę");
                break;
            } else {
                perror("msgrcv");
                break;
            }
        }

        sem_signal_safe(semid, SEM_KOLEJKA_MSG_SLOTS);
        
        // === OBSŁUGA PROŚBY ===
        if (prosba.jest_opiekunem) {
            log_info("[KASJER] Obsługuję opiekuna PID %d (wiek: %d, dziecko: %d lat, trasa: %d, powtórka: %s)",
                     prosba.zwiedzajacy_pid, prosba.wiek, prosba.wiek_dziecka, prosba.trasa,
                     prosba.czy_powrot ? "TAK" : "NIE");
        } else {
            log_info("[KASJER] Obsługuję PID %d (wiek: %d, trasa: %d, powtórka: %s)",
                     prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                     prosba.czy_powrot ? "TAK" : "NIE");
        }
        
        MsgBilet odpowiedz = prosba;
        odpowiedz.mtype = prosba.zwiedzajacy_pid;
        odpowiedz.timestamp = time(NULL);
        
        // WALIDACJA
        char powod_odmowy[128] = "";
        
        if (!waliduj_bilet(&prosba, powod_odmowy, sizeof(powod_odmowy), stan, semid)) {
            // ODMOWA
            odpowiedz.bilet_wydany = 0;
            odpowiedz.cena = 0.0f;
            strncpy(odpowiedz.powod_odmowy, powod_odmowy, sizeof(odpowiedz.powod_odmowy) - 1);
            
            log_warning("[KASJER] ODMOWA dla PID %d: %s",
                        prosba.zwiedzajacy_pid, powod_odmowy);
            
            log_to_file(LOG_BILETY, "PID=%d | Wiek=%d | Trasa=%d | Powtórka=%d | Cena=0.00 | Status=ODMOWA | Powód=%s",
                        prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                        prosba.czy_powrot, powod_odmowy);
        } else {
            // WYDAJ BILET
            odpowiedz.bilet_wydany = 1;
            odpowiedz.cena = oblicz_cene_biletu(&prosba);
            strcpy(odpowiedz.powod_odmowy, "");
            
            bilety_lokalne++;
            
            sem_wait_safe(semid, SEM_MUTEX);
            stan->bilety_sprzedane++;
            if (prosba.trasa == 1) {
                stan->bilety_trasa1++;
            } else {
                stan->bilety_trasa2++;
            }
            if (prosba.czy_powrot) {
                stan->bilety_powrot++;
            }
            sem_signal_safe(semid, SEM_MUTEX);
            
            const char *typ_biletu = okresl_typ_biletu(&prosba);
            
            log_success("[KASJER] Bilet #%d dla PID %d (%.2f zł, trasa %d, typ: %s)",
                        bilety_lokalne, prosba.zwiedzajacy_pid,
                        odpowiedz.cena, prosba.trasa, typ_biletu);
            
            if (prosba.jest_opiekunem) {
                log_to_file(LOG_BILETY, "PID=%d | Wiek=%d | Trasa=%d | Powtórka=%d | Dziecko=%d | Cena=%.2f | Status=OK | Numer=%d | Typ=%s",
                            prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                            prosba.czy_powrot, prosba.wiek_dziecka, odpowiedz.cena,
                            bilety_lokalne, typ_biletu);
            } else {
                log_to_file(LOG_BILETY, "PID=%d | Wiek=%d | Trasa=%d | Powtórka=%d | Cena=%.2f | Status=OK | Numer=%d | Typ=%s",
                            prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                            prosba.czy_powrot, odpowiedz.cena,
                            bilety_lokalne, typ_biletu);
            }
        }
        
        // Wyślij odpowiedź
        if (msgsnd(msgid, &odpowiedz, sizeof(MsgBilet) - sizeof(long), 0) < 0) {
            perror("msgsnd");
        }
        
        // Symulacja czasu obsługi
        usleep(losuj(300000, 800000));  // 0.3-0.8 sek
    }
    
    log_info("[KASJER] Jaskinia zamknięta - kończę pracę");
    
    // === PODSUMOWANIE ===
    printf("\n");
    sem_wait_safe(semid, SEM_MUTEX);
    printf("\n" COLOR_BOLD COLOR_BLUE);
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║       PODSUMOWANIE KASJERA                ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    printf(COLOR_RESET);
    printf("Bilety sprzedane:           %d\n", stan->bilety_sprzedane);
    printf("  - Trasa 1:                %d\n", stan->bilety_trasa1);
    printf("  - Trasa 2:                %d\n", stan->bilety_trasa2);
    printf("  - Powtórki (omijały):     %d\n", stan->bilety_powrot);
    printf("Odmowy (dzieci bez opieki): %d\n", stan->bilety_dzieci_bez_opieki);
    sem_signal_safe(semid, SEM_MUTEX);
    printf("\n");
    
    odlacz_pamiec_dzielona(stan);
    
    return 0;
}