#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"


// Flaga ewakuacji
volatile sig_atomic_t flaga_ewakuacja = 0;

// Obsługa sygnału ewakuacji
void obsluga_ewakuacji(int sig) {
    (void)sig;
    flaga_ewakuacja = 1;
}

// Parametry zwiedzającego
typedef struct {
    int wiek;
    int trasa;
    int czy_powrot;
    
    int jest_opiekunem;      // 1 jeśli ta osoba jest opiekunem
    int jest_dzieckiem;      // 1 jeśli ta osoba jest dzieckiem
    pid_t pid_opiekuna;      // PID opiekuna (jeśli dziecko)
    pid_t pid_dziecka;       // PID dziecka (jeśli opiekun)

} Zwiedzajacy;

int kup_bilet(Zwiedzajacy *zw, int msgid, pid_t moj_pid) {
    MsgBilet prosba;
    
    // Wybierz priorytet w kolejce
    if (zw->czy_powrot) {
        prosba.mtype = MSG_BILET_POWROT;  // 1 - najwyższy priorytet     
    } else {
        prosba.mtype = MSG_BILET_ZWYKLY;  // 2 - normalna kolejka
    }
    
    prosba.zwiedzajacy_pid = moj_pid;
    prosba.wiek = zw->wiek;
    prosba.trasa = zw->trasa;
    prosba.czy_powrot = zw->czy_powrot;
    prosba.jest_opiekunem = zw->jest_opiekunem;
    prosba.pid_opiekuna = zw->pid_opiekuna;
    prosba.pid_dziecka = zw->pid_dziecka;
    prosba.timestamp = time(NULL);
    
    const char *typ_kolejki = zw->czy_powrot ? "PRIORYTETOWA (powtórka)" : "ZWYKŁA";
    
    log_info("[ZWIEDZAJĄCY %d] Proszę o bilet (trasa %d, kolejka: %s)...", 
             moj_pid, zw->trasa, typ_kolejki);
    
    // Wyślij prośbę
    if (msgsnd(msgid, &prosba, sizeof(MsgBilet) - sizeof(long), 0) < 0) {
        perror("msgsnd");
        return 0;
    }
    
    // Czekaj na odpowiedź
    MsgBilet odpowiedz;
    int otrzymano = 0;
    time_t start_wait = time(NULL);
    
    while (!otrzymano && difftime(time(NULL), start_wait) < 5.0) {
        ssize_t ret = msgrcv(msgid, &odpowiedz, sizeof(MsgBilet) - sizeof(long),
                             moj_pid, IPC_NOWAIT);
        
        if (ret > 0) {
            otrzymano = 1;
            break;
        } else if (errno == ENOMSG) {
            usleep(100000);
            continue;
        } else {
            perror("msgrcv");
            return 0;
        }
    }
    
    if (!otrzymano) {
        log_error("[ZWIEDZAJĄCY %d] Timeout - kasjer nie odpowiedział", moj_pid);
        return 0;
    }
    
    if (!odpowiedz.bilet_wydany) {
        log_error("[ZWIEDZAJĄCY %d] Odmowa biletu: %s",
                  moj_pid, odpowiedz.powod_odmowy);
        return 0;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Mam bilet! (%.2f zł, trasa %d)",
                moj_pid, odpowiedz.cena, odpowiedz.trasa);
    
    return 1;
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    pid_t moj_pid = getpid();
    srand(moj_pid ^ time(NULL));
    
    signal(SIGTERM, obsluga_ewakuacji);
    
    // Pobierz IPC
    int shmid = atoi(getenv("SHMID"));
    int semid = atoi(getenv("SEMID"));
    int msgid = atoi(getenv("MSGID"));
    
    StanJaskini *stan = podlacz_pamiec_dzielona(shmid);
    
    if (!stan->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta", moj_pid);
        odlacz_pamiec_dzielona(stan);
        return 1;
    }
    
    zarejestruj_zwiedzajacego(stan, moj_pid, semid);
    
    // === GENERUJ PARAMETRY ===
    Zwiedzajacy zw;
    zw.wiek = losuj(WIEK_MIN, WIEK_MAX);
    zw.trasa = losuj(1, 2);
    zw.czy_powrot = 0;
    zw.jest_opiekunem = 0;
    zw.jest_dzieckiem = 0;
    zw.pid_opiekuna = 0;
    zw.pid_dziecka = 0;
    
    // Wariant 1: DOROSŁY Z DZIECKIEM (30% szans)
    if (zw.wiek >= WIEK_DOROSLY && losuj_szanse(SZANSA_OPIEKUN_Z_DZIECKIEM)) {
        zw.jest_opiekunem = 1;
        zw.trasa = 2;  // Dzieci tylko trasa 2
        // Utwórz proces dziecka
        pid_t pid_dziecko = fork();
        
        if (pid_dziecko == 0) {
            // === PROCES DZIECKA ===
            pid_t dziecko_pid = getpid();
            srand(dziecko_pid ^ time(NULL));
            
            Zwiedzajacy dziecko;
            dziecko.wiek = losuj(1, WIEK_TYLKO_TRASA2_DZIECKO - 1);  // 1-7 lat
            dziecko.trasa = 2;  // Dzieci tylko trasa 2
            dziecko.czy_powrot = 0;
            dziecko.jest_dzieckiem = 1;
            dziecko.jest_opiekunem = 0;
            dziecko.pid_opiekuna = moj_pid;  // PID rodzica
            dziecko.pid_dziecka = 0;
            
            log_info("[DZIECKO %d] Wiek: %d, Opiekun: PID %d", 
                     dziecko_pid, dziecko.wiek, moj_pid);
            
            zarejestruj_zwiedzajacego(stan, dziecko_pid, semid);
            
            // Dziecko kupuje bilet (z odniesieneim do opiekuna)
            if (kup_bilet(&dziecko, msgid, dziecko_pid)) {
                // Symuluj wycieczkę z opiekunem
                log_info("[DZIECKO %d] Wycieczka z opiekunem...", dziecko_pid);
                sleep(T2);
                log_success("[DZIECKO %d] Koniec wycieczki!", dziecko_pid);
            }
            
            wyrejestruj_zwiedzajacego(stan, dziecko_pid, semid);
            odlacz_pamiec_dzielona(stan);
            exit(0);
        } else if (pid_dziecko > 0) {
            // Rodzic (opiekun)
            zw.pid_dziecka = pid_dziecko;
            log_info("[OPIEKUN %d] Mam dziecko: PID %d", moj_pid, pid_dziecko);
        }
    }
    
    // Senior >75 lat: wymuś trasę 2
    if (zw.wiek > WIEK_TYLKO_TRASA2_SENIOR) {
    zw.trasa = 2;
    log_info("[ZWIEDZAJACY %d] Senior - wymuszona trasa 2", moj_pid);
    }
    
    log_info("[ZWIEDZAJĄCY %d] Wiek: %d, Trasa: %d, Typ: %s",
             moj_pid, zw.wiek, zw.trasa,
             zw.jest_opiekunem ? "OPIEKUN" : 
             (zw.wiek < WIEK_TYLKO_TRASA2_DZIECKO ? "DZIECKO" : "DOROSŁY"));
    
    // === KUPNO BILETU ===
kupno_biletu:
    
    if (flaga_ewakuacja || !stan->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Ewakuacja/zamknięcie", moj_pid);
        goto wyjscie;
    }
    
    if (!kup_bilet(&zw, msgid, moj_pid)) {
        goto wyjscie;
    }
    
    // === WYCIECZKA ===
    int czas_wycieczki = (zw.trasa == 1) ? T1 : T2;
    
    log_info("[ZWIEDZAJĄCY %d] Czekam na przewodnika...", moj_pid);
    sleep(losuj(1, 3));
    
    if (!stan->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta przed wycieczką", moj_pid);
        goto wyjscie;
    }
    
    log_info("[ZWIEDZAJĄCY %d] Wycieczka (trasa %d, %d sek)...",
             moj_pid, zw.trasa, czas_wycieczki);
    
    for (int i = 0; i < czas_wycieczki; i++) {
        if (flaga_ewakuacja) {
            log_warning("[ZWIEDZAJĄCY %d] Ewakuacja!", moj_pid);
            goto wyjscie;
        }
        sleep(1);
    }
    
    log_success("[ZWIEDZAJĄCY %d] Wycieczka zakończona!", moj_pid);
    
    // === POWTÓRKA (10% szans) ===
    if (!zw.czy_powrot && losuj_szanse(SZANSA_POWROT)) {
        if (!stan->jaskinia_otwarta) {
            log_info("[ZWIEDZAJĄCY %d] Chciałem powtórzyć, ale zamknięte", moj_pid);
            goto wyjscie;
        }
        
        log_info("[ZWIEDZAJĄCY %d] POWTÓRKA! (50%% zniżki, omijam kolejkę)", moj_pid);
        
        zw.czy_powrot = 1;
        zw.trasa = (zw.trasa == 1) ? 2 : 1;  // Inna trasa
        
        // Sprawdź regulamin
        if (zw.wiek < WIEK_TYLKO_TRASA2_DZIECKO || zw.wiek > WIEK_TYLKO_TRASA2_SENIOR) {
            zw.trasa = 2;
        }
        
        sleep(2);
        goto kupno_biletu;
    }
    
wyjscie:
    log_info("[ZWIEDZAJĄCY %d] Opuszczam jaskinię", moj_pid);
    
    // Jeśli opiekun - poczekaj na dziecko
    if (zw.jest_opiekunem && zw.pid_dziecka > 0) {
        log_info("[OPIEKUN %d] Czekam na dziecko PID %d...", moj_pid, zw.pid_dziecka);
        int status;
        waitpid(zw.pid_dziecka, &status, 0);
        log_info("[OPIEKUN %d] Dziecko wyszło", moj_pid);
    }
    
    wyrejestruj_zwiedzajacego(stan, moj_pid, semid);
    odlacz_pamiec_dzielona(stan);
    
    return 0;
}
