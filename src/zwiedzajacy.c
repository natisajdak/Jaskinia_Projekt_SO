#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
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

// GLOBALNE ZMIENNE I FLAGI
int shmid_global = -1;
int semid_global = -1;
StanJaskini *stan_global = NULL;
int trasa_global = 0;
volatile sig_atomic_t jestem_na_trasie = 0;

void obsluga_ewakuacji(int sig) {
    (void)sig;
    flaga_stop_ipc = 1;
}

// STRUKTURY
typedef struct {
    int wiek;
    int trasa;
    int czy_powrot;
    
    int jest_opiekunem;
    int wiek_dziecka;
    pthread_t watek_dziecko;
} Zwiedzajacy;

typedef struct {
    int wiek;
    int trasa;
    pid_t pid_opiekuna;
    pthread_mutex_t *mutex_opiekun;
    pthread_cond_t *cond_opiekun;
    volatile int *flaga_gotowy_wejscie;
    volatile int *flaga_gotowy_wyjscie;
    volatile int *flaga_koniec;
} ArgsDziecko;

void* funkcja_watku_dziecka(void* arg) {
    ArgsDziecko *a = (ArgsDziecko*)arg;
    pthread_t tid = pthread_self();
    
    log_info("[DZIECKO TID %lu] Wiek: %d, Opiekun PID: %d, Trasa: %d", 
             tid, a->wiek, a->pid_opiekuna, a->trasa );
    
    log_info("[DZIECKO TID %lu] Czekam na sygnał opiekuna (wejście)...", tid);
    
    // CZEKAJ NA SYGNAŁ OPIEKUNA: wchodzimy na trasę
    pthread_mutex_lock(a->mutex_opiekun);
    while (*(a->flaga_gotowy_wejscie) == 0 && *(a->flaga_koniec) == 0) {
        pthread_cond_wait(a->cond_opiekun, a->mutex_opiekun);
    }
    pthread_mutex_unlock(a->mutex_opiekun);
    
    if (*(a->flaga_koniec)) {
        log_warning("[DZIECKO TID %lu] Wycieczka anulowana wychodzę z opiekunem", tid);
        free(a);
        pthread_exit(NULL);
    }
    
    log_success("[DZIECKO TID %lu] Otrzymałem sygnał - jesteśmy na trasie!", tid);
    
    // WYCIECZKA (razem z opiekunem) 
    int czas_wycieczki = (a->trasa == 1) ? T1 : T2;
    log_info("[DZIECKO TID %lu] Zwiedzam trasę %d z opiekunem (%d sek)...", 
             tid, a->trasa, czas_wycieczki);
    
    sleep(czas_wycieczki);
    
    log_info("[DZIECKO TID %lu] Czekam na sygnał opiekuna (wyjście)...", tid);
    
    // CZEKAJ NA SYGNAŁ OPIEKUNA: "wychodzimy" 
    pthread_mutex_lock(a->mutex_opiekun);
    while (*(a->flaga_gotowy_wyjscie) == 0 && *(a->flaga_koniec) == 0) {
        pthread_cond_wait(a->cond_opiekun, a->mutex_opiekun);
    }
    pthread_mutex_unlock(a->mutex_opiekun);
    
    log_success("[DZIECKO TID %lu] Opuściłem jaskinię z opiekunem!", tid);
    
    free(a);
    pthread_exit(NULL);
}

int kup_bilet(Zwiedzajacy *zw, int msgid, pid_t moj_pid) {
    MsgBilet prosba;
    prosba.mtype = zw->czy_powrot ? MSG_BILET_POWROT : MSG_BILET_ZWYKLY;
    prosba.zwiedzajacy_pid = moj_pid;
    prosba.wiek = zw->wiek;
    prosba.trasa = zw->trasa;
    prosba.czy_powrot = zw->czy_powrot;
    prosba.jest_opiekunem = zw->jest_opiekunem;
    prosba.wiek_dziecka = zw->wiek_dziecka;
    prosba.timestamp = time(NULL);
    
    const char *typ_kolejki = zw->czy_powrot ? "PRIORYTETOWA (powtórka)" : "ZWYKŁA";
    
    if (zw->jest_opiekunem) {
        log_info("[OPIEKUN %d] Proszę o bilet (trasa %d, kolejka: %s, dziecko: %d lat)",
                 moj_pid, zw->trasa, typ_kolejki, zw->wiek_dziecka);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Proszę o bilet (trasa %d, kolejka: %s)",
                 moj_pid, zw->trasa, typ_kolejki);
    }
    
    // ═══ Czekaj na slot w kolejce komunikatów ═══
    log_info("[ZWIEDZAJĄCY %d] Czekam na wolny slot w kolejce komunikatów...", 
             moj_pid);
    
    if (sem_timed_wait_safe(semid_global, SEM_KOLEJKA_MSG_SLOTS, 60) != 0) {
        log_error("[ZWIEDZAJĄCY %d] Timeout oczekiwania na slot w kolejce!", moj_pid);
        return 0;
    }

    // Sprawdź czy jaskinia nadal otwarta
    if (flaga_stop_ipc || !stan_global->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta przed wysłaniem prośby", moj_pid);
        sem_signal_safe(semid_global, SEM_KOLEJKA_MSG_SLOTS);
        return 0;
    }
    
    log_info("[ZWIEDZAJĄCY %d] Mam slot - wysyłam prośbę", moj_pid);
    
    // ═══ BLOKUJĄCY msgsnd ═══
    while (1) {
        // Flaga 0: czekaj, aż będzie miejsce (blokuj)
        if (msgsnd(msgid, &prosba, sizeof(MsgBilet) - sizeof(long), 0) == 0) {
            break; // Sukces!
        }

        // Jeśli dotarliśmy tutaj, msgsnd zwrócił -1 (błąd)
        if (errno == EINTR) {
            log_warning("[ZWIEDZAJĄCY %d] msgsnd przerwany sygnałem - ponawiam próbę...", moj_pid);
            continue; // Wracamy na początek pętli i próbujemy wysłać ponownie
        } 
        
        // Obsługa innych błędów krytycznych (np. EIDRM - kolejka usunięta, EINVAL)
        perror("[ZWIEDZAJĄCY] Błąd krytyczny msgsnd");
        
        sem_signal_safe(semid_global, SEM_KOLEJKA_MSG_SLOTS);
        
        return 0; 
    }
    
    log_info("[ZWIEDZAJĄCY %d] Prośba wysłana - czekam na odpowiedź kasjera...", moj_pid);

    // ═══ BLOKUJĄCY msgrcv (czeka na odpowiedź kasjera) ═══
    MsgBilet odpowiedz;
    ssize_t ret = msgrcv(msgid, &odpowiedz, sizeof(MsgBilet) - sizeof(long),
                         moj_pid, 0);  // BLOKUJE

    if (ret < 0) {
        if (errno == EINTR) {
            log_warning("[ZWIEDZAJĄCY %d] Przerwano oczekiwanie (sygnał)", moj_pid);
        } else if (errno == EIDRM) {
            log_warning("[ZWIEDZAJĄCY %d] Kolejka usunięta", moj_pid);
        } else {
            perror("[ZWIEDZAJĄCY] msgrcv");
        }
        return 0;
    }
    
    // ═══ SPRAWDŹ ODPOWIEDŹ ═══
    if (!odpowiedz.bilet_wydany) {
        log_error("[ZWIEDZAJĄCY %d] Odmowa biletu: %s", 
                  moj_pid, odpowiedz.powod_odmowy);
        
        sem_wait_safe(semid_global, SEM_MUTEX);
        stan_global->licznik_odrzuconych++;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        return 0;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Mam bilet! (%.2f zł, trasa %d)",
                moj_pid, odpowiedz.cena, odpowiedz.trasa);
    
    return 1;
}

// MECHANIZM KŁADKI: semafor → kładka → semafor
int wejscie_na_kladke(int trasa, pid_t pid, int jest_opiekunem) {
    int sem_wejscie_allowed = (trasa == 1) ? 
        SEM_KLADKA1_WEJSCIE_ALLOWED : SEM_KLADKA2_WEJSCIE_ALLOWED;
    int sem_kladka = (trasa == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int sem_potw = (trasa == 1) ? SEM_POTWIERDZENIE_WEJSCIE_TRASA1 : SEM_POTWIERDZENIE_WEJSCIE_TRASA2;
    int sem_limit_trasy = (trasa == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;
    int liczba_miejsc = jest_opiekunem ? 2 : 1;
    
    if (jest_opiekunem) {
        log_info("[OPIEKUN %d] Czekam na otwarcie WEJŚCIA na kładkę (z dzieckiem = %d miejsca)...", 
                 pid, liczba_miejsc);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Czekam na otwarcie WEJŚCIA na kładkę...", pid);
    }
    
    if (flaga_stop_ipc) {
        for (int i = 0; i < liczba_miejsc; i++)
            sem_signal_safe(semid_global, sem_limit_trasy);
        return -1;
    }

    int ret = sem_timed_wait_safe(semid_global, sem_wejscie_allowed, 10);
    if (ret != 0) {

        if (!stan_global->jaskinia_otwarta || flaga_stop_ipc) {
            for (int i = 0; i < liczba_miejsc; i++) sem_signal_safe(semid_global, sem_limit_trasy);
        return -1; 
        }

        log_error("[ZWIEDZAJĄCY %d] TIMEOUT wejścia - przewodnik nie otworzył!", pid);
        
        // Zwolnij zarezerwowane miejsca na trasie
        for (int i = 0; i < liczba_miejsc; i++) {
            sem_signal_safe(semid_global, sem_limit_trasy);
        }
        return -1; 
    }

    if (!stan_global->jaskinia_otwarta || flaga_stop_ipc) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta - opuszczam kolejkę i wychodzę", pid);
        for (int i = 0; i < liczba_miejsc; i++) {
            sem_signal_safe(semid_global, sem_limit_trasy);
        }
        return -1;
    }

    log_info("[ZWIEDZAJĄCY %d] Wejście otwarte - (%d miejsc)", pid, liczba_miejsc);
    
    // REZERWUJ miejsca na kładce
    struct sembuf op;

    op.sem_num = sem_kladka;
    op.sem_op  = -liczba_miejsc;
    op.sem_flg = SEM_UNDO;

    semop(semid_global, &op, 1);
    
    log_info("[ZWIEDZAJĄCY %d] Zająłem %d miejsc na kładce WEJŚCIE", pid, liczba_miejsc);
    
    
    // AKTUALIZUJ licznik kładki
    sem_wait_safe(semid_global, SEM_MUTEX);
    if (trasa == 1) {
        stan_global->kladka1_licznik += liczba_miejsc;
        log_info("[ZWIEDZAJĄCY %d] Na kładce 1 WEJŚCIE: %d/%d osób", 
                 pid, stan_global->kladka1_licznik, K);
    } else {
        stan_global->kladka2_licznik += liczba_miejsc;
        log_info("[ZWIEDZAJĄCY %d] Na kładce 2 WEJŚCIE: %d/%d osób", 
                 pid, stan_global->kladka2_licznik, K);
    }
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    if (jest_opiekunem) {
        log_success("[OPIEKUN %d] Wszedłem na kładkę z dzieckiem kierunek WEJŚCIE", pid);
    } else {
        log_success("[ZWIEDZAJĄCY %d] Wszedłem na kładkę kierunek WEJŚCIE", pid);
    }
    
    // PRZEJŚCIE przez kładkę 
    int czas_przejscia = losuj(800000, 1500000);
    usleep(czas_przejscia);
    
    log_info("[ZWIEDZAJĄCY %d] Przechodzę przez kładkę...", pid);

    // Zejście z kładki na trasę 
    sem_wait_safe(semid_global, SEM_MUTEX);
    if (trasa == 1) {
        stan_global->kladka1_licznik -= liczba_miejsc;
        stan_global->trasa1_licznik += liczba_miejsc; 
        log_info("[ZWIEDZAJĄCY %d] Wszedłem na trasę 1 (na trasie: %d/%d)", 
                 pid, stan_global->trasa1_licznik, N1);
    } else {
        stan_global->kladka2_licznik -= liczba_miejsc;
        stan_global->trasa2_licznik += liczba_miejsc; 
        log_info("[ZWIEDZAJĄCY %d] Wszedłem na trasę 2 (na trasie: %d/%d)", 
                 pid, stan_global->trasa2_licznik, N2);
    }
    sem_signal_safe(semid_global, SEM_MUTEX);
    usleep(1000);
    
    // ZWOLNIJ miejsca na kładce
    op.sem_op = +liczba_miejsc;
    semop(semid_global, &op, 1);
    
    // Potwierdzenie
    log_info("[ZWIEDZAJĄCY %d] Wysyłam %d potwierdzeń do przewodnika...", 
             pid, liczba_miejsc);
    
    for (int i = 0; i < liczba_miejsc; i++) {
        sem_signal_safe(semid_global, sem_potw); 
    }
    
    if (jest_opiekunem) {
        log_success("[OPIEKUN %d] Zwiedzam trasę %d z dzieckiem...", pid, trasa);
    } else {
        log_success("[ZWIEDZAJĄCY %d] Zwiedzam trasę %d...", pid, trasa);
    }

    return 0;
}

void wyjscie_z_kladki(int trasa, pid_t pid, int jest_opiekunem) {
    int sem_wyjscie_allowed = (trasa == 1) ? 
        SEM_KLADKA1_WYJSCIE_ALLOWED : SEM_KLADKA2_WYJSCIE_ALLOWED;
    int sem_kladka = (trasa == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int sem_potw = (trasa == 1) ? SEM_POTWIERDZENIE_WYJSCIE_TRASA1 : SEM_POTWIERDZENIE_WYJSCIE_TRASA2;
    int sem_limit_trasy = (trasa == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;
    int liczba_miejsc = jest_opiekunem ? 2 : 1;
    
    if (jest_opiekunem) {
        log_info("[OPIEKUN %d] Czekam na otwarcie WYJŚCIA (z dzieckiem = %d miejsca)...", 
                 pid, liczba_miejsc);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Czekam na otwarcie WYJŚCIA z kładki...", pid);
    }
    
    sem_wait_safe(semid_global, sem_wyjscie_allowed);
    log_info("[ZWIEDZAJĄCY %d] Wyjście otwarte - schodzę z trasy", pid);
    
    // 2. REZERWUJ miejsca na kładce
    struct sembuf op;

    op.sem_num = sem_kladka;
    op.sem_op  = -liczba_miejsc;
    op.sem_flg = SEM_UNDO;

    semop(semid_global, &op, 1);
    
    log_info("[ZWIEDZAJĄCY %d] Zająłem %d miejsc na kładce WYJŚCIE", pid, liczba_miejsc);
    
    // ZWOLNIJ semafor wyjścia
    sem_signal_safe(semid_global, sem_wyjscie_allowed);
    
    // Przenieś z trasy na kładkę
    sem_wait_safe(semid_global, SEM_MUTEX);
    if (trasa == 1) {
        stan_global->trasa1_licznik -= liczba_miejsc;  
        stan_global->kladka1_licznik += liczba_miejsc;
        log_info("[ZWIEDZAJĄCY %d] Na kładce 1 WYJŚCIE: %d/%d osób", 
                 pid, stan_global->kladka1_licznik, K);
    } else {
        stan_global->trasa2_licznik -= liczba_miejsc;  
        stan_global->kladka2_licznik += liczba_miejsc;
        log_info("[ZWIEDZAJĄCY %d] Na kładce 2 WYJŚCIE: %d/%d osób", 
                 pid, stan_global->kladka2_licznik, K);
    }
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    if (jest_opiekunem) {
        log_success("[OPIEKUN %d] Wszedłem na kładkę z dzieckiem kierunek WYJŚCIE", pid);
    } else {
        log_success("[ZWIEDZAJĄCY %d] Wszedłem na kładkę kierunek WYJŚCIE", pid);
    }
    
    // PRZEJŚCIE przez kładkę
    int czas_przejscia = losuj(800000, 1500000);
    usleep(czas_przejscia);
    
    log_info("[ZWIEDZAJĄCY %d] Schodzę z kładki...", pid);
    
    sem_wait_safe(semid_global, SEM_MUTEX);
    if (trasa == 1) {
        stan_global->kladka1_licznik -= liczba_miejsc; 
    } else {
        stan_global->kladka2_licznik -= liczba_miejsc;  
    }
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    // Małe opóźnienie na synchronizację pamięci dzielonej
    usleep(1000);  // 1ms

    // ZWOLNIJ miejsca na kładce 
    op.sem_op = +liczba_miejsc;
    semop(semid_global, &op, 1);
    
    // Potwierdzenie
    log_info("[ZWIEDZAJĄCY %d] Wysyłam %d potwierdzeń wyjścia...", 
             pid, liczba_miejsc);
    
    for (int i = 0; i < liczba_miejsc; i++) {
        sem_signal_safe(semid_global, sem_potw); 
    }
    
    if (jest_opiekunem) {
        log_success("[OPIEKUN %d] OPUŚCIŁEM jaskinię z dzieckiem!", pid);
    } else {
        log_success("[ZWIEDZAJĄCY %d] OPUŚCIŁEM jaskinię!", pid);
    }

    for (int i = 0; i < liczba_miejsc; i++) {
    sem_signal_safe(semid_global, sem_limit_trasy);
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    pid_t moj_pid = getpid();
    srand(moj_pid ^ time(NULL));
    
    struct sigaction sa;
    sa.sa_handler = obsluga_ewakuacji;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    
    shmid_global = atoi(getenv("SHMID"));
    semid_global = atoi(getenv("SEMID"));
    int msgid = atoi(getenv("MSGID"));
    
    StanJaskini *stan = podlacz_pamiec_dzielona(shmid_global);
    if (!stan) {
        log_error("[ZWIEDZAJĄCY %d] Nie mogę podłączyć pamięci dzielonej", moj_pid);
        _exit(1);
    }
    stan_global = stan;
    
    if (!stan->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta", moj_pid);
        odlacz_pamiec_dzielona(stan);
        return 1;
    }

    // GENERUJ PARAMETRY ZWIEDZAJĄCEGO
    Zwiedzajacy zw;
    zw.wiek = losuj(WIEK_MIN, WIEK_MAX);
    zw.trasa = losuj(1, 2);
    zw.czy_powrot = 0;
    zw.jest_opiekunem = 0;
    zw.wiek_dziecka = 0;

   // Decydujemy o statusie opiekuna PRZED rejestracją w SHM
    int lokalna_flaga_opiekun = (zw.wiek >= WIEK_DOROSLY && losuj_szanse(SZANSA_OPIEKUN_Z_DZIECKIEM));
    
    if (zw.wiek < WIEK_TYLKO_TRASA2_DZIECKO && !lokalna_flaga_opiekun) {
    log_warning("[DZIECKO %d] Wiek %d - muszę mieć opiekuna! Odchodzę",
                moj_pid, zw.wiek);
    sem_wait_safe(semid_global, SEM_MUTEX);
    stan->licznik_odrzuconych++;
    stan->licznik_zakonczonych++;
    sem_signal_safe(semid_global, SEM_MUTEX);
    sem_signal_safe(semid_global, SEM_ZAKONCZENI);
    odlacz_pamiec_dzielona(stan);
    sem_signal_safe(semid_global, SEM_WOLNE_SLOTY_ZWIEDZAJACYCH);
    return 1;
    }
    // REJESTRACJA
    int moj_indeks = zarejestruj_zwiedzajacego(stan, moj_pid, semid_global, lokalna_flaga_opiekun);
    if (moj_indeks == -1) {
    sem_wait_safe(semid_global, SEM_MUTEX);
    stan->licznik_zakonczonych++; 
    sem_signal_safe(semid_global, SEM_MUTEX);
    sem_signal_safe(semid_global, SEM_ZAKONCZENI);
    odlacz_pamiec_dzielona(stan);
    _exit(1);
    }

    // Mutex i cond dla synchronizacji z wątkiem dziecka
    pthread_mutex_t mutex_opiekun = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_opiekun = PTHREAD_COND_INITIALIZER;
    volatile int flaga_gotowy_wejscie = 0;
    volatile int flaga_gotowy_wyjscie = 0;
    volatile int flaga_koniec = 0;

    // Jeśli wylosowano opiekuna, twórz wątek
    if (lokalna_flaga_opiekun) {
        zw.jest_opiekunem = 1;
        zw.trasa = 2;
        zw.wiek_dziecka = losuj(1, WIEK_TYLKO_TRASA2_DZIECKO - 1);
        
        log_info("[OPIEKUN %d] Jestem z dzieckiem (wiek: %d) - oboje trasa %d",
                 moj_pid, zw.wiek_dziecka, zw.trasa);

        ArgsDziecko *args = malloc(sizeof(ArgsDziecko));
        if (args) {
            args->wiek = zw.wiek_dziecka;
            args->trasa = zw.trasa;
            args->pid_opiekuna = moj_pid;
            args->mutex_opiekun = &mutex_opiekun;
            args->cond_opiekun = &cond_opiekun;
            args->flaga_gotowy_wejscie = &flaga_gotowy_wejscie;
            args->flaga_gotowy_wyjscie = &flaga_gotowy_wyjscie;
            args->flaga_koniec = &flaga_koniec;
            
            if (pthread_create(&zw.watek_dziecko, NULL, funkcja_watku_dziecka, args) != 0) {
                free(args);
                zw.jest_opiekunem = 0;
            } else {
                log_success("[OPIEKUN %d] Utworzono wątek dziecka (TID=%lu)",
                            moj_pid, (unsigned long)zw.watek_dziecko);
            }
        }
    }
    
    if (zw.jest_opiekunem) {
        sem_wait_safe(semid_global, SEM_MUTEX);
        stan->zwiedzajacy_jest_opiekunem[moj_indeks] = 1; 
        sem_signal_safe(semid_global, SEM_MUTEX);
    }
    
    if (zw.jest_opiekunem) {
    log_info("[OPIEKUN %d] Zarejestrowano mnie jako opiekun", moj_pid);
    }

    // SENIOR >75 lat: wymuś trasę 2
    if (zw.wiek > WIEK_TYLKO_TRASA2_SENIOR) {
        log_info("[SENIOR %d] Senior - wymuszona trasa 2", moj_pid);
        zw.trasa = 2;
    }

    log_info("[ZWIEDZAJĄCY %d] Wiek: %d, Trasa: %d, Typ: %s",
             moj_pid, zw.wiek, zw.trasa,
             zw.jest_opiekunem ? "OPIEKUN+DZIECKO(wątek)" : "ZWYKŁY");
    
    
kupno_biletu:
    
    if (flaga_stop_ipc || !stan->jaskinia_otwarta) {
        log_warning("[ZWIEDZAJĄCY %d] Ewakuacja/zamknięcie", moj_pid);
        goto wyjscie;
    }
    
    // KUPNO BILETU
    if (!kup_bilet(&zw, msgid, moj_pid)) {
        if(zw.jest_opiekunem) {
            // Anuluj wątek dziecka
            pthread_mutex_lock(&mutex_opiekun);
            flaga_koniec = 1;
            pthread_cond_broadcast(&cond_opiekun);
            pthread_mutex_unlock(&mutex_opiekun);
        }
        log_warning("[ZWIEDZAJĄCY %d] Nie dostałem biletu - odchodzę", moj_pid);
        goto wyjscie;
    }

    // DOŁĄCZ DO KOLEJKI
    dolacz_do_kolejki(zw.trasa, moj_pid, stan, semid_global);
    
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Czekam w kolejce z dzieckiem na trasę %d...",
                 moj_pid, zw.trasa);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Czekam w kolejce na trasę %d...", moj_pid, zw.trasa);
    }
    
    // CZEKAJ NA PRZEWODNIKA
    int sem_gotowa = (zw.trasa == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
    
    log_info("[ZWIEDZAJĄCY %d] Czekam aż przewodnik zbierze grupę...", moj_pid);
    sem_wait_safe(semid_global, sem_gotowa);
    
    if (!stan->jaskinia_otwarta || flaga_stop_ipc) {
        log_warning("[ZWIEDZAJĄCY %d] Jaskinia zamknięta - opuszczam kolejkę i wychodzę", moj_pid);
        goto wyjscie;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Przewodnik zabrał mnie w trasę!", moj_pid);
    
    if (wejscie_na_kladke(zw.trasa, moj_pid, zw.jest_opiekunem) != 0) {
    log_warning("[ZWIEDZAJĄCY %d] Nie udało się wejść na kładkę - jaskinia zamknięta. Wychodzę", moj_pid);
    goto wyjscie; 
    }

    // Sygnalizuj dziecku: "jesteśmy na trasie"
    if (zw.jest_opiekunem) {
        pthread_mutex_lock(&mutex_opiekun);
        flaga_gotowy_wejscie = 1;
        pthread_cond_broadcast(&cond_opiekun);
        pthread_mutex_unlock(&mutex_opiekun);
        
        log_info("[OPIEKUN %d] Dałem sygnał dziecku: jesteśmy na trasie!", moj_pid);
    }

    sem_wait_safe(semid_global, SEM_MUTEX);
    stan_global->liczba_wejsc++;
    sem_signal_safe(semid_global, SEM_MUTEX);

    // WYCIECZKA
    trasa_global = zw.trasa;
    jestem_na_trasie = 1;
    
    int czas_wycieczki = (zw.trasa == 1) ? T1 : T2;
    
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Zwiedzam trasę %d z dzieckiem (%d sek) ═══",
                 moj_pid, zw.trasa, czas_wycieczki);
    } else {
        log_info("[ZWIEDZAJĄCY %d] Zwiedzam trasę %d (%d sek) ═══",
                 moj_pid, zw.trasa, czas_wycieczki);
    }
    
    sleep(czas_wycieczki);
    jestem_na_trasie = 0;
    
    if (flaga_stop_ipc) {
        log_warning("[ZWIEDZAJĄCY %d] Ewakuacja po wycieczce!", moj_pid);
        goto wyjscie;
    }
    
    log_success("[ZWIEDZAJĄCY %d] Wycieczka zakończona!", moj_pid);

    // WYJŚCIE: TRASA → KŁADKA → NA ZEWNĄTRZ
    wyjscie_z_kladki(zw.trasa, moj_pid, zw.jest_opiekunem);

    sem_wait_safe(semid_global, SEM_MUTEX);
    stan_global->liczba_wyjsc++;
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    // Sygnalizuj dziecku: "wyszliśmy"
    if (zw.jest_opiekunem) {
        pthread_mutex_lock(&mutex_opiekun);
        flaga_gotowy_wyjscie = 1;
        pthread_cond_broadcast(&cond_opiekun);
        pthread_mutex_unlock(&mutex_opiekun);
        
        log_info("[OPIEKUN %d] Dałem sygnał dziecku: wyszliśmy!", moj_pid);
    }
    
    // POWTÓRKA (10% szans) - NIE DLA OPIEKUNÓW
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
    

    // ZAKOŃCZENIE 
wyjscie:
    if (zw.jest_opiekunem) {
        log_info("[OPIEKUN %d] Czekam na zakończenie wątku dziecka...", moj_pid);
        
        pthread_mutex_lock(&mutex_opiekun);
        flaga_koniec = 1;
        pthread_cond_broadcast(&cond_opiekun);
        pthread_mutex_unlock(&mutex_opiekun);
        pthread_join(zw.watek_dziecko, NULL);
        
        log_success("[OPIEKUN %d] Dziecko-wątek zakończone - wychodzimy razem!", moj_pid);
        
        pthread_mutex_destroy(&mutex_opiekun);
        pthread_cond_destroy(&cond_opiekun);
        
        // ═══ WYREJESTRUJ OPIEKUNA Z TABLICY ═══
        sem_wait_safe(semid_global, SEM_MUTEX);
        stan->zwiedzajacy_jest_opiekunem[moj_indeks] = 0;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_info("[OPIEKUN %d] Wyrejestrowano jako opiekun", moj_pid);
    }

    sem_wait_safe(semid_global, SEM_MUTEX);
    stan->licznik_zakonczonych++; 
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    wyrejestruj_zwiedzajacego(stan, moj_pid, semid_global);
    odlacz_pamiec_dzielona(stan);
    sem_signal_safe(semid_global, SEM_ZAKONCZENI);

    return 0;
}