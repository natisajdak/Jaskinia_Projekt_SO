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
    
    int jest_opiekunem;
    int jest_dzieckiem;
    pid_t pid_opiekuna;
    pid_t pid_dziecka;
} Zwiedzajacy;

int kup_bilet(Zwiedzajacy *zw, int msgid, pid_t moj_pid) {
    MsgBilet prosba;
    
    if (zw->czy_powrot) {
        prosba.mtype = MSG_BILET_POWROT;
    } else {
        prosba.mtype = MSG_BILET_ZWYKLY;
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
    
    if (msgsnd(msgid, &prosba, sizeof(MsgBilet) - sizeof(long), 0) < 0) {
        perror("msgsnd");
        return 0;
    }
    
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

void wycieczka_dziecka(pid_t dziecko_pid, int trasa, int msgid, StanJaskini *stan, int semid) {
    Zwiedzajacy dziecko;
    dziecko.wiek = losuj(1, WIEK_TYLKO_TRASA2_DZIECKO - 1);
    dziecko.trasa = trasa;  // TĄ SAMĄ CO OPIEKUN
    dziecko.czy_powrot = 0;
    dziecko.jest_dzieckiem = 1;
    dziecko.jest_opiekunem = 0;
    dziecko.pid_opiekuna = getppid();  // PID rodzica (opiekuna)
    dziecko.pid_dziecka = 0;
    
    log_info("[DZIECKO %d] Wiek: %d, Opiekun: PID %d, Trasa: %d", 
             dziecko_pid, dziecko.wiek, dziecko.pid_opiekuna, trasa);
    
    zarejestruj_zwiedzajacego(stan, dziecko_pid, semid);
    
    if (!kup_bilet(&dziecko, msgid, dziecko_pid)) {
        wyrejestruj_zwiedzajacego(stan, dziecko_pid, semid);
        exit(1);
    }
    
    // DZIECKO NIE DOŁĄCZA - opiekun doda ich oboje atomowo!
    // Tylko czeka na przewodnika
    int sem_gotowa = (dziecko.trasa == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
    log_info("[DZIECKO %d] Czekam na przewodnika razem z opiekunem...", dziecko_pid);
    sem_wait_safe(semid, sem_gotowa);
    
    if (!stan->jaskinia_otwarta || flaga_ewakuacja) {
        log_warning("[DZIECKO %d] Jaskinia zamknięta - wychodzę z opiekunem", dziecko_pid);
        wyrejestruj_zwiedzajacego(stan, dziecko_pid, semid);
        exit(0);
    }
    
    log_success("[DZIECKO %d] Przewodnik zabrał nas w trasę!", dziecko_pid);
    
    // WEJŚCIE NA KŁADKĘ
    int sem_wejscie = (dziecko.trasa == 1) ? SEM_GRUPA1_WEJSCIE_KLADKA : SEM_GRUPA2_WEJSCIE_KLADKA;
    
    log_info("[DZIECKO %d] Wchodzę na kładkę z opiekunem", dziecko_pid);
    sem_wait_safe(semid, sem_wejscie);
    sem_signal_safe(semid, SEM_POTWIERDZENIE);
    
    // WYCIECZKA
    int czas_wycieczki = (dziecko.trasa == 1) ? T1 : T2;
    log_info("[DZIECKO %d] Zwiedzam z grupą i opiekunem (%d sek)...", dziecko_pid, czas_wycieczki);
    sleep(czas_wycieczki);
    
    if (flaga_ewakuacja) {
        log_warning("[DZIECKO %d] Ewakuacja podczas wycieczki!", dziecko_pid);
        wyrejestruj_zwiedzajacego(stan, dziecko_pid, semid);
        exit(0);
    }
    
    log_success("[DZIECKO %d] Wycieczka zakończona!", dziecko_pid);
    
    // WYJŚCIE
    int sem_wyjscie = (dziecko.trasa == 1) ? SEM_GRUPA1_WYJSCIE_KLADKA : SEM_GRUPA2_WYJSCIE_KLADKA;
    
    log_info("[DZIECKO %d] Wychodzę z jaskini razem z opiekunem", dziecko_pid);
    sem_wait_safe(semid, sem_wyjscie);
    sem_signal_safe(semid, SEM_POTWIERDZENIE);
    
    log_success("[DZIECKO %d] Opuściłem jaskinię z opiekunem", dziecko_pid);
    
    wyrejestruj_zwiedzajacego(stan, dziecko_pid, semid);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    pid_t moj_pid = getpid();
    srand(moj_pid ^ time(NULL));
    
    signal(SIGTERM, obsluga_ewakuacji);
    
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
        zw.trasa = 2;  // Opiekun z dzieckiem zawsze trasa 2
        
        pid_t pid_dziecko = fork();
        
        if (pid_dziecko == 0) {
            // === PROCES DZIECKA ===
            pid_t dziecko_pid = getpid();
            srand(dziecko_pid ^ time(NULL));
            
            // Dziecko przechodzi TĘ SAMĄ trasę co opiekun
            wycieczka_dziecka(dziecko_pid, zw.trasa, msgid, stan, semid);
            
            odlacz_pamiec_dzielona(stan);
            exit(0);
        } else if (pid_dziecko > 0) {
            zw.pid_dziecka = pid_dziecko;
            log_info("[OPIEKUN %d] Mam dziecko: PID %d (oboje trasa %d)", moj_pid, pid_dziecko, zw.trasa);
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
    
    if (zw.jest_opiekunem && zw.pid_dziecka > 0) {
    // Poczekaj aż dziecko kupi bilet
    sleep(1);
    
    // ATOMOWO dodaj parę do kolejki
    if (!dolacz_pare_do_kolejki(zw.trasa, zw.pid_dziecka, moj_pid, stan, semid)) {
        log_error("[OPIEKUN %d] Nie można dołączyć do kolejki!", moj_pid);
        goto wyjscie;
    }
    
    // Obudź przewodnika
    int sem_kolejka = (zw.trasa == 1) ? 
        SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
    sem_signal_safe(semid, sem_kolejka);
    
    log_info("[OPIEKUN %d] Czekam w kolejce z dzieckiem PID %d na trasę %d...", 
             moj_pid, zw.pid_dziecka, zw.trasa);
    } else {
    // Normalnie - pojedyncza osoba
    dolacz_do_kolejki(zw.trasa, moj_pid, stan, semid);
    int sem_kolejka = (zw.trasa == 1) ? 
        SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
    sem_signal_safe(semid, sem_kolejka);
    
    log_info("[ZWIEDZAJĄCY %d] Czekam w kolejce na trasę %d...", moj_pid, zw.trasa);
}
    
    // Blokuj tutaj aż przewodnik zbierze grupę i wyśle sygnał
    int sem_gotowa = (zw.trasa == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
    sem_wait_safe(semid, sem_gotowa);
    
    // Sprawdź czy jaskinia otwarta
    if (!stan->jaskinia_otwarta || flaga_ewakuacja) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta - opuszczam kolejkę i wychodzę", moj_pid);
        goto wyjscie;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Przewodnik zabrał mnie w trasę!", moj_pid);
    
    // === WEJŚCIE NA KŁADKĘ ===
    int sem_wejscie = (zw.trasa == 1) ? SEM_GRUPA1_WEJSCIE_KLADKA : SEM_GRUPA2_WEJSCIE_KLADKA;
    
    log_info("[ZWIEDZAJĄCY %d] Czekam na sygnał wejścia na kładkę...", moj_pid);
    sem_wait_safe(semid, sem_wejscie);
    
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Wchodzę na kładkę z dzieckiem", moj_pid);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Wchodzę na kładkę", moj_pid);
    }
    
    // Potwierdzenie że jestem na kładce
    sem_signal_safe(semid, SEM_POTWIERDZENIE);
    
    // === WYCIECZKA (PASYWNE CZEKANIE) ===
    int czas_wycieczki = (zw.trasa == 1) ? T1 : T2;
    
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Zwiedzam trasę %d z dzieckiem (%d sek)...", 
                 moj_pid, zw.trasa, czas_wycieczki);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Zwiedzam trasę %d (%d sek)...", 
                 moj_pid, zw.trasa, czas_wycieczki);
    }
    
    sleep(czas_wycieczki);
    
    if (flaga_ewakuacja) {
        log_warning("[ZWIEDZAJĄCY %d] Ewakuacja podczas wycieczki!", moj_pid);
        goto wyjscie;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Wycieczka zakończona!", moj_pid);
    
    // === WYJŚCIE Z JASKINI ===
    int sem_wyjscie = (zw.trasa == 1) ? SEM_GRUPA1_WYJSCIE_KLADKA : SEM_GRUPA2_WYJSCIE_KLADKA;
    
    log_info("[ZWIEDZAJĄCY %d] Czekam na sygnał wyjścia...", moj_pid);
    sem_wait_safe(semid, sem_wyjscie);
    
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Wychodzę przez kładkę z dzieckiem", moj_pid);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Wychodzę przez kładkę", moj_pid);
    }
    
    sem_signal_safe(semid, SEM_POTWIERDZENIE);
    
    log_success("[ZWIEDZAJĄCY %d] Opuściłem jaskinię", moj_pid);
    
    // === POWTÓRKA (10% szans) - ALE NIE DLA OPIEKUNÓW Z DZIEĆMI ===
    if (!zw.czy_powrot && !zw.jest_opiekunem && losuj_szanse(SZANSA_POWROT)) {
        if (!stan->jaskinia_otwarta) {
            log_info("[ZWIEDZAJĄCY %d] Chciałem powtórzyć, ale zamknięte", moj_pid);
            goto wyjscie;
        }
        
        log_info("[ZWIEDZAJĄCY %d] POWTÓRKA! (50%% zniżki, omijam kolejkę)", moj_pid);
        
        zw.czy_powrot = 1;
        zw.trasa = (zw.trasa == 1) ? 2 : 1;
        
        // Sprawdź regulamin
        if (zw.wiek < WIEK_TYLKO_TRASA2_DZIECKO || zw.wiek > WIEK_TYLKO_TRASA2_SENIOR) {
            zw.trasa = 2;
        }
        
        sleep(2);
        goto kupno_biletu;
    }
    
wyjscie:
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Opuszczam jaskinię i czekam na dziecko...", moj_pid);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Opuszczam jaskinię (koniec)", moj_pid);
    }
    
    // Jeśli opiekun - poczekaj na dziecko (wychodzą RAZEM)
    if (zw.jest_opiekunem && zw.pid_dziecka > 0) {
        log_info("[OPIEKUN %d] Czekam na dziecko PID %d...", moj_pid, zw.pid_dziecka);
        int status;
        waitpid(zw.pid_dziecka, &status, 0);
        log_success("[OPIEKUN %d] Dziecko wyszło - wychodzimy razem!", moj_pid);
    }
    
    wyrejestruj_zwiedzajacego(stan, moj_pid, semid);
    odlacz_pamiec_dzielona(stan);
    
    return 0;
}