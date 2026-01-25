#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// GLOBALNE ZMIENNE
int numer_trasy;
int shmid_global, semid_global;
StanJaskini *stan_global = NULL;

volatile sig_atomic_t flaga_zamkniecie = 0;

// Zablokuj nowe wejścia na trasę (wyczerpanie semafora)
void zablokuj_wejscia_na_trase(int trasa) {
    int sem_limit = (trasa == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;

    int max_iteracji = (trasa == 1) ? N1 : N2;
    
    for (int i = 0; i < max_iteracji; i++) {
        if (sem_trywait_safe(semid_global, sem_limit) != 0) {
            break; 
        }
    }

    log_warning("[PRZEWODNIK %d] Zablokowano nowe wejścia na trasę (semafor limitu wyczerpany)", trasa);
}

void anuluj_zebrana_grupe(int trasa) {

    sem_wait_safe(semid_global, SEM_MUTEX);

    int grupa_aktywna = (trasa == 1) ? stan_global->grupa1_czeka_na_wpuszczenie : stan_global->grupa2_czeka_na_wpuszczenie;
    if (!grupa_aktywna) {
        sem_signal_safe(semid_global, SEM_MUTEX);
        return;
    }

    int liczba_miejsc = 0;

    if (trasa == 1) {
        for (int i = 0; i < N1; i++) {
            if (stan_global->grupa1_pids[i] > 0)
                liczba_miejsc++;
        }
    } else {
        for (int i = 0; i < N2; i++) {
            if (stan_global->grupa2_pids[i] > 0)
                liczba_miejsc++;
        }
    }

    log_warning("[PRZEWODNIK %d] ANULOWANIE zebranej grupy (%d osób)",
                trasa, liczba_miejsc);

    sem_signal_safe(semid_global, SEM_MUTEX);

    // semafor do obudzenia
    int sem_ready = (trasa == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;

    for (int i = 0; i < liczba_miejsc; i++) {
        sem_signal_safe(semid_global, sem_ready);
    }

    // czyszczenie stanu mutex
    sem_wait_safe(semid_global, SEM_MUTEX);

    if (trasa == 1) {
        stan_global->grupa1_liczba = 0;
        stan_global->grupa1_czeka_na_wpuszczenie = 0;
        for (int i = 0; i < N1; i++)
            stan_global->grupa1_pids[i] = 0;
    } else {
        stan_global->grupa2_liczba = 0;
        stan_global->grupa2_czeka_na_wpuszczenie = 0;
        for (int i = 0; i < N2; i++)
            stan_global->grupa2_pids[i] = 0;
    }

    sem_signal_safe(semid_global, SEM_MUTEX);

    log_info("[PRZEWODNIK %d] Grupa anulowana i wyczyszczona", trasa);
}

void obsluga_zamkniecia(int sig) {
    (void)sig;
    
    flaga_zamkniecie = 1;
    flaga_stop_ipc = 1; 
}
void czekaj_na_pusta_kladke(int trasa) {
    log_info("[PRZEWODNIK %d] Sprawdzam czy kładka pusta...", trasa);
    
    // Zwiększony timeout przy zamknięciu
    int timeout = flaga_zamkniecie ? 120 : 60; // 2 min vs 1 min
    int elapsed = 0;
    
    while (elapsed < timeout) {
        sem_wait_safe(semid_global, SEM_MUTEX);
        int licznik = (trasa == 1) ? stan_global->kladka1_licznik : stan_global->kladka2_licznik;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        if (licznik <= 0) {
            log_success("[PRZEWODNIK %d] Kładka pusta", trasa);
            return;
        }
        
        // Nie przerywaj przy zamknięciu - kładka musi być pusta
        if (flaga_zamkniecie && elapsed % 10 == 0) {
            log_warning("[PRZEWODNIK %d] Zamknięcie aktywne - czekam aż kładka się opróżni (%d/120s, na kładce: %d)", 
                        trasa, elapsed, licznik);
        }
        
        sleep(1);
        elapsed++;
    }
    
    // Timeout - błąd krytyczny
    sem_wait_safe(semid_global, SEM_MUTEX);
    int licznik = (trasa == 1) ? stan_global->kladka1_licznik : stan_global->kladka2_licznik;
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    log_error("[PRZEWODNIK %d] TIMEOUT: Kładka nie opróżniła się w %d sekund! (nadal %d osób)", 
              trasa, timeout, licznik);
    flaga_zamkniecie = 1;
    return;
}

void wpusc_przez_kladke(int liczba_miejsc, int liczba_procesow) {
    int sem_wejscie = (numer_trasy == 1) ? 
        SEM_KLADKA1_WEJSCIE_ALLOWED : SEM_KLADKA2_WEJSCIE_ALLOWED;
    int sem_wyjscie = (numer_trasy == 1) ? 
        SEM_KLADKA1_WYJSCIE_ALLOWED : SEM_KLADKA2_WYJSCIE_ALLOWED;
    int sem_potw = (numer_trasy == 1) ? SEM_POTWIERDZENIE_WEJSCIE_TRASA1 : SEM_POTWIERDZENIE_WEJSCIE_TRASA2;
    
    log_info("[PRZEWODNIK %d] ═══ WPUSZCZANIE PRZEZ KŁADKĘ (%d miejsc na trasie) ═══", 
             numer_trasy, liczba_miejsc);
    
    // 1. Zamknij wyjście
    log_info("[PRZEWODNIK %d] Zamykam wyjście...", numer_trasy);
    ustaw_semafor_na_zero(semid_global, sem_wyjscie);
    
    // 2. Czekaj aż kładka pusta
    czekaj_na_pusta_kladke(numer_trasy);
    
    //  3. Sprawdz czy zamknięcie 
    if (flaga_zamkniecie) {
        log_warning("[PRZEWODNIK %d] Zamknięcie przed otwarciem wejścia - ANULACJA!", numer_trasy);
        anuluj_zebrana_grupe(numer_trasy);
        return;
    }
    
    // 4. Otwórz wejście dla wszystkich procesów w grupie
    log_info("[PRZEWODNIK %d] Otwieram wejście dla %d procesow...",
         numer_trasy, liczba_procesow);

    for (int i = 0; i < liczba_procesow; i++) {
        sem_signal_safe(semid_global, sem_wejscie);
    }

    log_success("[PRZEWODNIK %d] WEJŚCIE OTWARTE - %d miejsc",
            numer_trasy, liczba_miejsc);

    // 5. Czekaj na potwierdzenia 
    log_info("[PRZEWODNIK %d] Czekam na %d potwierdzeń zajęcia miejsc na trasie......", 
             numer_trasy, liczba_miejsc);
    
    int otrzymane_potwierdzenia = 0;
    
    for (int i = 0; i < liczba_miejsc; i++) {
 
        int timeout = flaga_zamkniecie ? 30 : 15;
        int ret = sem_timed_wait_safe(semid_global, sem_potw, timeout);
        
        if (ret != 0) {
            log_error("[PRZEWODNIK %d] Brak potwierdzenia %d/%d (Timeout %ds)!", 
                      numer_trasy, i+1, liczba_miejsc, timeout);
            continue;
        }
        
        otrzymane_potwierdzenia++;

        // Logowanie postępu
        sem_wait_safe(semid_global, SEM_MUTEX);
        int na_trasie_log = (numer_trasy == 1) ? stan_global->trasa1_licznik : stan_global->trasa2_licznik;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_info("[PRZEWODNIK %d] [%d/%d] zwiedzających przeszło (na trasie: %d)", 
                 numer_trasy, i+1, liczba_miejsc, na_trasie_log);
    }
    
    // Sprawdź czy wszystkie potwierdzenia otrzymano
    if (otrzymane_potwierdzenia < liczba_miejsc) {
        log_error("[PRZEWODNIK %d] Otrzymano tylko %d/%d potwierdzeń wejścia!",
                  numer_trasy, otrzymane_potwierdzenia, liczba_miejsc);
    }
    
    // 6. Zamknij wejście
    log_info("[PRZEWODNIK %d] Zamykam wejście...", numer_trasy);
    ustaw_semafor_na_zero(semid_global, sem_wejscie);
    
    // 7. Weryfikacja stanu
    sem_wait_safe(semid_global, SEM_MUTEX);
    int na_trasie = (numer_trasy == 1) ? 
        stan_global->trasa1_licznik : stan_global->trasa2_licznik;
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    // Weryfikacja - porównaj z otrzymanymi potwierdzeniami
    if (na_trasie == otrzymane_potwierdzenia) {
        log_success("[PRZEWODNIK %d] Wszyscy na trasie! (na trasie: %d/%d)", 
                    numer_trasy, na_trasie, liczba_miejsc);
    } else {
        log_error("[PRZEWODNIK %d] BŁĄD: licznik trasy (%d) != otrzymane potwierdzenia (%d)!", 
                  numer_trasy, na_trasie, otrzymane_potwierdzenia);
    }
}


void wypusc_przez_kladke(int liczba_miejsc, int liczba_procesow) {
    int sem_wejscie = (numer_trasy == 1) ? 
        SEM_KLADKA1_WEJSCIE_ALLOWED : SEM_KLADKA2_WEJSCIE_ALLOWED;
    int sem_wyjscie = (numer_trasy == 1) ? 
        SEM_KLADKA1_WYJSCIE_ALLOWED : SEM_KLADKA2_WYJSCIE_ALLOWED;
    int sem_potw = (numer_trasy == 1) ? SEM_POTWIERDZENIE_WYJSCIE_TRASA1 : SEM_POTWIERDZENIE_WYJSCIE_TRASA2;
    
    log_info("[PRZEWODNIK %d] ═══ WYPUSZCZANIE PRZEZ KŁADKĘ (%d miejsc) ═══", 
             numer_trasy, liczba_miejsc);
    
    // 1. Zamknij wejście
    log_info("[PRZEWODNIK %d] Zamykam wejście...", numer_trasy);
    ustaw_semafor_na_zero(semid_global, sem_wejscie);
    
    // 2. Czekaj aż kładka pusta 
    czekaj_na_pusta_kladke(numer_trasy);
    
    // 3. Otwórz wyjście dla procesów
    for (int i = 0; i < liczba_procesow; i++) {
        sem_signal_safe(semid_global, sem_wyjscie);
    }
    log_success("[PRZEWODNIK %d] WYJŚCIE OTWARTE - zwiedzający mogą wychodzie", numer_trasy);
    
    // 4. ZAWSZE czekaj na wszystkie potwierdzenia (nawet przy zamknięciu)
    log_info("[PRZEWODNIK %d] Czekam aż %d zwiedzających opuści jaskinię...", 
             numer_trasy, liczba_miejsc);
    
    int otrzymane_potwierdzenia = 0;
    int mialy_timeout = 0;
    
    for (int i = 0; i < liczba_miejsc; i++) {
        // Zwiększony timeout przy zamknięciu (60s vs 30s)
        int timeout = flaga_zamkniecie ? 60 : 30;
        
        int ret = sem_timed_wait_safe(semid_global, sem_potw, timeout);
        
        if (ret != 0) {
            log_error("[PRZEWODNIK %d] Brak potwierdzenia wyjścia %d/%d (Timeout %ds)!", 
                      numer_trasy, i+1, liczba_miejsc, timeout);
            mialy_timeout = 1;
            continue;
        }
        
        otrzymane_potwierdzenia++;
        log_info("[PRZEWODNIK %d] [%d/%d] zwiedzających opuściło jaskinię", 
                numer_trasy, i+1, liczba_miejsc);
    }
    
    // Raportuj problem jeśli były timeouty
    if (mialy_timeout) {
        log_error("[PRZEWODNIK %d] Otrzymano tylko %d/%d potwierdzeń wyjścia!",
                  numer_trasy, otrzymane_potwierdzenia, liczba_miejsc);
    }
    
    // 5. Zamknij wyjście
    log_info("[PRZEWODNIK %d] Zamykam wyjście...", numer_trasy);
    ustaw_semafor_na_zero(semid_global, sem_wyjscie);
    
    // 6. Weryfikacja stanu
    sem_wait_safe(semid_global, SEM_MUTEX);
    int na_trasie = (numer_trasy == 1) ? 
        stan_global->trasa1_licznik : stan_global->trasa2_licznik;
    int na_kladce = (numer_trasy == 1) ? 
        stan_global->kladka1_licznik : stan_global->kladka2_licznik;
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    if (na_trasie == 0 && na_kladce == 0 && !mialy_timeout) {
        log_success("[PRZEWODNIK %d] Wszyscy opuścili jaskinię! (na trasie: %d, na kładce: %d)", 
                    numer_trasy, na_trasie, na_kladce);
    } else if (mialy_timeout) {
        log_warning("[PRZEWODNIK %d] Wypuszczanie z TIMEOUTAMI (na trasie: %d, na kładce: %d)", 
                    numer_trasy, na_trasie, na_kladce);
    } else {
        log_error("[PRZEWODNIK %d] BŁĄD: trasa=%d (oczekiwano 0), kładka=%d (oczekiwano 0)!", 
                  numer_trasy, na_trasie, na_kladce);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Użycie: %s <numer_trasy>\n", argv[0]);
        return 1;
    }
    
    numer_trasy = atoi(argv[1]);
    if (numer_trasy != 1 && numer_trasy != 2) {
        fprintf(stderr, "Błąd: numer trasy musi być 1 lub 2\n");
        return 1;
    }
    
    log_info("[PRZEWODNIK %d] STARTUJE (PID: %d)", 
             numer_trasy, getpid());

    shmid_global = atoi(getenv("SHMID"));
    semid_global = atoi(getenv("SEMID"));
    stan_global = podlacz_pamiec_dzielona(shmid_global);
    
    if (numer_trasy == 1) {
        stan_global->pid_przewodnik1 = getpid();
    } else {
        stan_global->pid_przewodnik2 = getpid();
    }
    
    log_success("[PRZEWODNIK %d] Połączono z IPC (shmid=%d, semid=%d)", 
                numer_trasy, shmid_global, semid_global);
    
    // ═══ OBSŁUGA SYGNAŁÓW ═══
    struct sigaction sa;
    sa.sa_handler = obsluga_zamkniecia;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    
    log_info("[PRZEWODNIK %d] Zarejestrowano obsługę sygnałów", numer_trasy);
    
    // ═══ PARAMETRY TRASY ═══
    int czas_trasy = (numer_trasy == 1) ? T1 : T2;
    int max_na_trasie = (numer_trasy == 1) ? N1 : N2;
    int numer_wycieczki = 0;
    int w_trakcie_wycieczki = (numer_trasy == 1) ? stan_global->grupa1_aktywna : stan_global->grupa2_aktywna;
    
    log_info("[PRZEWODNIK %d] Parametry: czas=%ds, max_osób=%d, pojemność_kładki=%d", 
             numer_trasy, czas_trasy, max_na_trasie, K);

    // GŁÓWNA PĘTLA WYCIECZEK
    log_info("[PRZEWODNIK %d] Rozpoczynam pracę - czekam na zwiedzających...", numer_trasy);
    
    while (!flaga_zamkniecie && stan_global->jaskinia_otwarta) {
        int sem_kolejka = (numer_trasy == 1) ? 
            SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
        
        // ═══ CZEKAJ AŻ KTOŚ POJAWI SIĘ W KOLEJCE ═══
        log_info("[PRZEWODNIK %d] Czekam na zwiedzających w kolejce...", numer_trasy);
        sem_wait_safe(semid_global, sem_kolejka);
        
        if(flaga_zamkniecie || !stan_global->jaskinia_otwarta) {
            log_warning("[PRZEWODNIK %d] Jaskinia zamknięta - kończę pracę", numer_trasy);
            break;
        }
        
        if (flaga_zamkniecie || !stan_global->jaskinia_otwarta) {
            log_warning("[PRZEWODNIK %d] Jaskinia zamknięta - przerywam czekanie", numer_trasy);
            break;
        }
        
        log_info("[PRZEWODNIK %d] Wykryto zwiedzających w kolejce - zbieram grupę", numer_trasy);
        
        // SPRAWDŹ ZAMKNIĘCIE PRZED ZBIERANIEM
            if (flaga_zamkniecie) {
            log_warning("[PRZEWODNIK %d] Zamknięcie przed wejściem – inicjuję procedurę kończenia", numer_trasy);

            zablokuj_wejscia_na_trase(numer_trasy);

            int grupa_czeka = (numer_trasy == 1) ? stan_global->grupa1_czeka_na_wpuszczenie : stan_global->grupa2_czeka_na_wpuszczenie;

            if (grupa_czeka && !w_trakcie_wycieczki) {
                anuluj_zebrana_grupe(numer_trasy);
            }
        }

        // ZBIERZ GRUPĘ
        int liczba_procesow = zbierz_grupe(numer_trasy, stan_global, semid_global, max_na_trasie);
        
        if (liczba_procesow == 0) {
            log_warning("[PRZEWODNIK %d] Nie zebrano grupy - sprawdzam ponownie za 1s", numer_trasy);
            sleep(1);
            
            if (flaga_zamkniecie || !stan_global->jaskinia_otwarta) {
                log_info("[PRZEWODNIK %d] Jaskinia zamknięta - przerywam", numer_trasy);
                break;
            }
            
            continue;
        }
        
        // OZNACZ ŻE GRUPA ZEBRANA, ALE NIE WPUSZCZONA
        sem_wait_safe(semid_global, SEM_MUTEX);
        int liczba_miejsc_w_grupie = (numer_trasy == 1) ? 
            stan_global->grupa1_liczba : stan_global->grupa2_liczba;
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->grupa1_czeka_na_wpuszczenie = 1;
        } else {
            stan_global->grupa2_czeka_na_wpuszczenie = 1;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);

        // PONOWNIE SPRAWDŹ ZAMKNIĘCIE
        if (flaga_zamkniecie) {
            log_warning("[PRZEWODNIK %d] Zamknięcie PO zebraniu grupy - ANULACJA!", numer_trasy);
            int sem_gotowa = (numer_trasy == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
            for (int i = 0; i < liczba_procesow; i++) {
                sem_signal_safe(semid_global, sem_gotowa); 
            }
            
            anuluj_zebrana_grupe(numer_trasy);
            break;
        }
        
        // ═══ WERYFIKACJA CZY PROCESY ISTNIEJĄ ═══
        sem_wait_safe(semid_global, SEM_MUTEX);
        int grupa_ok = 1;
        for (int i = 0; i < liczba_procesow; i++) {
            pid_t pid = (numer_trasy == 1) ? 
                stan_global->grupa1_pids[i] : stan_global->grupa2_pids[i];
            
            if(pid <= 0 || kill(pid, 0) != 0) {
                log_warning("[PRZEWODNIK %d] Zwiedzający (PID=%d) w grupie nie istnieje - anulacja", 
                            numer_trasy, pid);
                grupa_ok = 0;
                break;
            }
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        if (!grupa_ok) {
            anuluj_zebrana_grupe(numer_trasy);
            continue;
        }
        
        log_info("[PRZEWODNIK %d] Zebrano grupę: %d osób (%d procesów, max: %d)", 
                 numer_trasy, liczba_miejsc_w_grupie, liczba_procesow, max_na_trasie);
        
        numer_wycieczki++;
        
        log_info("[PRZEWODNIK %d] ══════════ WYCIECZKA #%d ══════════", numer_trasy, numer_wycieczki);
        log_success("[PRZEWODNIK %d] Grupa zebrana: %d osób (max: %d)", 
                    numer_trasy, liczba_miejsc_w_grupie, max_na_trasie);
        
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                    "Wycieczka #%d: START (grupa: %d osób)", numer_wycieczki, liczba_miejsc_w_grupie);
        
        // ═══ OZNACZ ŻE PRZEWODNIK CZEKA NA GRUPĘ ═══
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->trasa1_czeka_na_grupe = 1;
            stan_global->trasa1_wycieczka_nr = numer_wycieczki;
        } else {
            stan_global->trasa2_czeka_na_grupe = 1;
            stan_global->trasa2_wycieczka_nr = numer_wycieczki;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // ═══ POWIADOM PROCESY ═══
        int sem_gotowa = (numer_trasy == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
        
        log_info("[PRZEWODNIK %d] Powiadamiam %d procesów zwiedzających...", 
                 numer_trasy, liczba_procesow);
        
        for (int i = 0; i < liczba_procesow; i++) {
            sem_signal_safe(semid_global, sem_gotowa);
        }
        
        log_success("[PRZEWODNIK %d] Powiadomiłem wszystkich zwiedzających", numer_trasy);
        
        // OSTATNIE SPRAWDZENIE przed wpuszczeniem
        if (flaga_zamkniecie || !stan_global->jaskinia_otwarta) {
            log_warning("[PRZEWODNIK %d] Zamknięcie PO powiadomieniu – ANULACJA",
                        numer_trasy);
            anuluj_zebrana_grupe(numer_trasy);
            break;
        }

        // ═══ WPUSZCZANIE PRZEZ KŁADKĘ (WEJŚCIE) ═══
        wpusc_przez_kladke(liczba_miejsc_w_grupie, liczba_procesow);
        
        // Oznacz że grupa NIE czeka już na wpuszczenie (została wpuszczona)
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->grupa1_czeka_na_wpuszczenie = 0;
        } else {
            stan_global->grupa2_czeka_na_wpuszczenie = 0;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // Jeśli zamknięcie nastąpiło podczas wpuszczania
        if (flaga_zamkniecie) {
            log_warning("[PRZEWODNIK %d] Zamknięcie podczas wpuszczania - dokańczam wycieczkę", numer_trasy);
        }
        
        // ═══ ROZPOCZĘCIE WYCIECZKI ═══
        w_trakcie_wycieczki = 1;
        
        log_info("[PRZEWODNIK %d] ═══ TRASA W TOKU (czas: %d sek) ═══", 
                 numer_trasy, czas_trasy);
        
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->grupa1_aktywna = 1;
        } else {
            stan_global->grupa2_aktywna = 1;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                    "Wycieczka #%d: ZWIEDZANIE (%d osób, %d sek)", 
                    numer_wycieczki, liczba_miejsc_w_grupie, czas_trasy);
        
        // ═══ ZWIEDZANIE ═══
        sleep(czas_trasy);

        if (flaga_zamkniecie) {
            log_warning("[PRZEWODNIK %d] Zamknięcie PODCZAS wycieczki – DOKAŃCZAM", numer_trasy);
        }
        
        log_success("[PRZEWODNIK %d] Zwiedzanie zakończone", numer_trasy);
        
        w_trakcie_wycieczki = 0;
        
        // ═══ POWRÓT PRZEZ KŁADKĘ (WYJŚCIE) ═══
        log_info("[PRZEWODNIK %d] Powrót grupy przez kładkę...", numer_trasy);
        wypusc_przez_kladke(liczba_miejsc_w_grupie, liczba_procesow);
        
        // ═══ RESET STANU GRUPY ═══
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->grupa1_aktywna = 0;
            stan_global->trasa1_czeka_na_grupe = 0;
            for (int i = 0; i < liczba_procesow; i++) {
                stan_global->grupa1_pids[i] = 0;
            }
            stan_global->grupa1_liczba = 0;
        } else {
            stan_global->grupa2_aktywna = 0;
            stan_global->trasa2_czeka_na_grupe = 0;
            for (int i = 0; i < liczba_procesow; i++) {
                stan_global->grupa2_pids[i] = 0;
            }
            stan_global->grupa2_liczba = 0;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                    "Wycieczka #%d: KONIEC (sukces)", numer_wycieczki);
        
        log_success("[PRZEWODNIK %d] Wycieczka #%d zakończona SUKCESEM", 
                    numer_trasy, numer_wycieczki);
        
        // ═══ SPRAWDŹ CZY ZAMKNIĘCIE PO WYCIECZCE ═══
        if (flaga_zamkniecie) {
            log_info("[PRZEWODNIK %d] Sygnał zamknięcia PO zakończeniu wycieczki - kończę pracę", 
                     numer_trasy);
            break;
        }
        
        sleep(1);
    }
    
    log_info("[PRZEWODNIK %d] Zakończono prowadzenie wycieczek", numer_trasy);
    log_success("[PRZEWODNIK %d] Łącznie przeprowadzono: %d wycieczek", 
                numer_trasy, numer_wycieczki);
    
    log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                "PODSUMOWANIE: Łącznie %d wycieczek", numer_wycieczki);
    
     sem_wait_safe(semid_global, SEM_MUTEX);
    
    // Zbuduj cały tekst w buforze
    char podsumowanie[2048];
    snprintf(podsumowanie, sizeof(podsumowanie),
        "\n" COLOR_BOLD COLOR_GREEN
        "╔═══════════════════════════════════════╗\n"
        "║   PODSUMOWANIE PRZEWODNIKA TRASY %d    ║\n"
        "╚═══════════════════════════════════════╝\n"
        COLOR_RESET
        "Przeprowadzono wycieczek: %d\n"
        "Czas trasy:               %d sekund\n"
        "Max osób na trasie:       %d\n"
        "Max osób na kładce:       %d\n\n",
        numer_trasy,
        numer_wycieczki,
        czas_trasy,
        (numer_trasy == 1 ? N1 : N2),
        K
    );
    
    printf("%s", podsumowanie);
    fflush(stdout);
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    sem_wait_safe(semid_global, SEM_MUTEX);
    int bilety_trasa = (numer_trasy == 1) ? 
        stan_global->bilety_trasa1 : stan_global->bilety_trasa2;
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    printf("Bilety sprzedane (trasa %d): %d\n", numer_trasy, bilety_trasa);
    printf("\n");
    fflush(stdout);
    
    odlacz_pamiec_dzielona(stan_global);
    fflush(stdout);
    return 0;
}