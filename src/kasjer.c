#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Struktura argumentów dla wątku
typedef struct {
    int watek_id;
    int shmid;
    int semid;
    int msgid;
} WatekArgs;

// Mutex dla wątków (licznik biletów)
pthread_mutex_t mutex_bilety = PTHREAD_MUTEX_INITIALIZER;
int bilety_lokalne = 0;  // Licznik lokalny wątków

int sprawdz_opiekuna(MsgBilet *prosba) {
    if (prosba->pid_opiekuna > 0) {
        log_info("[KASJER] Dziecko PID %d ma opiekuna PID %d", 
                 prosba->zwiedzajacy_pid, prosba->pid_opiekuna);
        return 1;
    }
    
    // Jeśli dziecko <8 lat i nie ma opiekuna - ODMOWA
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO && prosba->pid_opiekuna == 0) {
        return 0;
    }
    
    return 1;
}

int waliduj_bilet(MsgBilet *prosba, char *powod, size_t powod_size, StanJaskini *stan, int semid) {
    // 1. Sprawdź opiekuna (dla dzieci <8 lat)
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO) {
        if (!sprawdz_opiekuna(prosba)) {
            snprintf(powod, powod_size, 
                     "Dzieci poniżej %d lat wymagają opiekuna (osoby dorosłej ≥%d lat)",
                     WIEK_TYLKO_TRASA2_DZIECKO, WIEK_DOROSLY);
            
            // Statystyka
            sem_wait_safe(semid, SEM_MUTEX);
            stan->bilety_dzieci_bez_opieki++;
            sem_signal_safe(semid, SEM_MUTEX);
            
            return 0;
        }
    }
    
    // 2. Dzieci <8 lat: tylko trasa 2
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO && prosba->trasa != 2) {
        snprintf(powod, powod_size,
                 "Dzieci poniżej %d lat mogą zwiedzać tylko trasę 2", 
                 WIEK_TYLKO_TRASA2_DZIECKO);
        return 0;
    }
    
    // 3. Seniorzy >75 lat: tylko trasa 2
    if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR && prosba->trasa != 2) {
        snprintf(powod, powod_size,
                 "Osoby powyżej %d lat mogą zwiedzać tylko trasę 2",
                 WIEK_TYLKO_TRASA2_SENIOR);
        return 0;
    }
    
    return 1; // OK
}

float oblicz_cene_biletu(MsgBilet *prosba) {
    // 1. Dzieci <3 lat: GRATIS
    if (prosba->wiek < WIEK_GRATIS) {
        return 0.0f;
    }
    
    // 2. Powtórka: 50% zniżki
    if (prosba->czy_powrot) {
        return CENA_BAZOWA * ZNIZKA_POWROT;
    }
    
    // 3. Dziecko z opiekunem: 20% zniżki (bonus)
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO && prosba->pid_opiekuna > 0) {
        return CENA_BAZOWA * (1.0f - ZNIZKA_DZIECKO_Z_OPIEKUNEM);
    }
    
    // 4. Senior >75 lat: 30% zniżki
    if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR) {
        return CENA_BAZOWA * 0.7f;
    }
    
    // 5. Normalny bilet
    return CENA_BAZOWA;
}

const char* okresl_typ_biletu(MsgBilet *prosba) {
    if (prosba->czy_powrot) {
        return "POWTÓRKA";
    }
    
    if (prosba->wiek < WIEK_GRATIS) {           // <3 lat
        return "DZIECKO_GRATIS";
    }
    
    if (prosba->wiek < WIEK_TYLKO_TRASA2_DZIECKO) {  // <8 lat
        if (prosba->pid_opiekuna > 0) {
            return "DZIECKO_Z_OPIEKUNEM";
        }
    }
    
    if (prosba->wiek > WIEK_TYLKO_TRASA2_SENIOR) {  // >75 lat
        return "SENIOR";
    }
    
    if (prosba->jest_opiekunem) {
        return "OPIEKUN";
    }
    
    return "NORMALNY";
}

void* obsluga_klienta(void* arg) {
    WatekArgs *args = (WatekArgs*)arg;
    StanJaskini *stan = podlacz_pamiec_dzielona(args->shmid);
    
    if (stan == (StanJaskini*)-1) {
    log_error("[KASJER-WATEK %d] Nie mozna podlaczyc pamieci", args->watek_id);
    free(args);
    pthread_exit(NULL);
    }

    log_info("[KASJER-WĄTEK %d] Start (TID=%lu)", args->watek_id, pthread_self());
    
    while (stan->jaskinia_otwarta) {
        MsgBilet prosba;
        
        ssize_t ret = msgrcv(args->msgid, &prosba, sizeof(MsgBilet) - sizeof(long), -2, IPC_NOWAIT);
        
        if (ret < 0) {
            if (errno == ENOMSG) {
                usleep(100000);  // 100ms
                continue;
            } else {
                perror("msgrcv");
                break;
            }
        }
        
        // === OBSŁUGA PROŚBY ===
        log_info("[KASJER-WĄTEK %d] Obsługuję PID %d (wiek: %d, trasa: %d, powtórka: %s, opiekun_PID: %d)",
                 args->watek_id, prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                 prosba.czy_powrot ? "TAK" : "NIE",
                 prosba.pid_opiekuna);
        
        MsgBilet odpowiedz = prosba;
        odpowiedz.mtype = prosba.zwiedzajacy_pid;
        odpowiedz.timestamp = time(NULL);
        
        // WALIDACJA
        char powod_odmowy[128] = "";
        
        if (!waliduj_bilet(&prosba, powod_odmowy, sizeof(powod_odmowy), stan, args->semid)) {
            // ODMOWA
            odpowiedz.bilet_wydany = 0;
            odpowiedz.cena = 0.0f;
            strncpy(odpowiedz.powod_odmowy, powod_odmowy, sizeof(odpowiedz.powod_odmowy) - 1);
            
            log_warning("[KASJER-WĄTEK %d] ODMOWA dla PID %d: %s",
                        args->watek_id, prosba.zwiedzajacy_pid, powod_odmowy);
            
            log_to_file(LOG_BILETY, "PID=%d | Wiek=%d | Trasa=%d | Powtórka=%d | Cena=0.00 | Status=ODMOWA | Powód=%s",
                        prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa, 
                        prosba.czy_powrot, powod_odmowy);
        } else {
            // WYDAJ BILET
            odpowiedz.bilet_wydany = 1;
            odpowiedz.cena = oblicz_cene_biletu(&prosba);
            strcpy(odpowiedz.powod_odmowy, "");
            
            // Zwiększ liczniki
            pthread_mutex_lock(&mutex_bilety);
            bilety_lokalne++;
            int numer_biletu = bilety_lokalne;
            pthread_mutex_unlock(&mutex_bilety);
            
            sem_wait_safe(args->semid, SEM_MUTEX);
            stan->bilety_sprzedane++;
            if (prosba.trasa == 1) {
                stan->bilety_trasa1++;
            } else {
                stan->bilety_trasa2++;
            }
            if (prosba.czy_powrot) {
                stan->bilety_powrot++;
            }
            sem_signal_safe(args->semid, SEM_MUTEX);
            
            const char *typ_biletu = okresl_typ_biletu(&prosba);
            
            log_success("[KASJER-WĄTEK %d] Bilet #%d dla PID %d (%.2f zł, trasa %d, typ: %s)",
                        args->watek_id, numer_biletu, prosba.zwiedzajacy_pid,
                        odpowiedz.cena, prosba.trasa, typ_biletu);
            
            log_to_file(LOG_BILETY, "PID=%d | Wiek=%d | Trasa=%d | Powtórka=%d | Opiekun_PID=%d | Cena=%.2f | Status=OK | Numer=%d | Typ=%s",
                        prosba.zwiedzajacy_pid, prosba.wiek, prosba.trasa,
                        prosba.czy_powrot, prosba.pid_opiekuna, odpowiedz.cena, 
                        numer_biletu, typ_biletu);
        }
        
        // Wyślij odpowiedź
        if (msgsnd(args->msgid, &odpowiedz, sizeof(MsgBilet) - sizeof(long), 0) < 0) {
            perror("msgsnd");
        }
        
        // Symulacja czasu obsługi
        usleep(losuj(300000, 800000));  // 0.3-0.8 sek
    }
    
    log_info("[KASJER-WĄTEK %d] Kończę pracę", args->watek_id);
    
    odlacz_pamiec_dzielona(stan);
    free(args);
    pthread_exit(NULL);
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
    
    // Utwórz 3 wątki
    const int NUM_WATKOW = 3;
    pthread_t watki[NUM_WATKOW];
    
    for (int i = 0; i < NUM_WATKOW; i++) {
        WatekArgs *args = malloc(sizeof(WatekArgs));
        args->watek_id = i + 1;
        args->shmid = shmid;
        args->semid = semid;
        args->msgid = msgid;
        
        if (pthread_create(&watki[i], NULL, obsluga_klienta, args) != 0) {
            perror("pthread_create");
            exit(1);
        }
    }
    
    log_success("[KASJER] Uruchomiono %d wątków obsługi", NUM_WATKOW);
    
    // Czekaj na wątki
    for (int i = 0; i < NUM_WATKOW; i++) {
        pthread_join(watki[i], NULL);
    }
    
    log_success("[KASJER] Podsumowanie: sprzedano %d biletów (trasa1: %d, trasa2: %d, powtórki: %d, odmowy dzieci: %d)",
                stan->bilety_sprzedane, stan->bilety_trasa1, stan->bilety_trasa2,
                stan->bilety_powrot, stan->bilety_dzieci_bez_opieki);
    
    // === PODSUMOWANIE KASJERA ===
    printf("\n");

    printf("╔═══════════════════════════════════════════╗\n");
    printf("║       PODSUMOWANIE KASJERA                ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
    
    sem_wait_safe(semid, SEM_MUTEX);
    printf("Bilety sprzedane:           %d\n", stan->bilety_sprzedane);
    printf("  - Trasa 1:                %d\n", stan->bilety_trasa1);
    printf("  - Trasa 2:                %d\n", stan->bilety_trasa2);
    printf("  - Powtórki (omijały):     %d\n", stan->bilety_powrot);
    printf("Odmowy (dzieci bez opieki): %d\n", stan->bilety_dzieci_bez_opieki);
    sem_signal_safe(semid, SEM_MUTEX);
    
    printf("\n");

    odlacz_pamiec_dzielona(stan);
    pthread_mutex_destroy(&mutex_bilety);
    
    return 0;
}