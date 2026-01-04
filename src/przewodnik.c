#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include "../include/config.h"
#include "../include/ipc.h"
#include "../include/utils.h"

// Parametry przewodnika
int numer_trasy;  // 1 lub 2
int shmid_global, semid_global;
StanJaskini *stan_global = NULL;

// Flaga zamknięcia (sygnał)
volatile sig_atomic_t flaga_zamkniecie = 0;

volatile sig_atomic_t w_trakcie_wycieczki = 0;

// Obsługa sygnału zamknięcia
void obsluga_zamkniecia(int sig) {
    if (sig == SIGUSR1 || sig == SIGUSR2) {
        flaga_zamkniecie = 1;
        if (w_trakcie_wycieczki) {
            log_warning("[PRZEWODNIK %d] Otrzymano sygnał zamknięcia PODCZAS wycieczki - dokończymy normalnie!", numer_trasy);
        } else {
            log_warning("[PRZEWODNIK %d] Otrzymano sygnał zamknięcia PRZED wyjściem - wycieczka ANULOWANA!", numer_trasy);
        }
    }
}

// WEJŚCIE PRZEZ KŁADKĘ 
void wpusc_przez_kladke(pid_t *grupa_pids, int liczba_osob) {
    (void)grupa_pids;
    
    int sem_kladka = (numer_trasy == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int sem_wejscie = (numer_trasy == 1) ? SEM_GRUPA1_WEJSCIE_KLADKA : SEM_GRUPA2_WEJSCIE_KLADKA;
    
    log_info("[PRZEWODNIK %d] Wpuszczam %d osób przez kładkę (max %d jednocześnie)...",
             numer_trasy, liczba_osob, K);

    // SPRAWDŹ I USTAW KIERUNEK KŁADKI
    sem_wait_safe(semid_global, SEM_MUTEX);
    int *kierunek = (numer_trasy == 1) ? &stan_global->kladka1_kierunek : &stan_global->kladka2_kierunek;
    int *licznik = (numer_trasy == 1) ? &stan_global->kladka1_licznik : &stan_global->kladka2_licznik;
    
    // Czekaj aż kładka będzie PUSTA (licznik=0) zanim zmienisz kierunek
    while (*licznik > 0) {
        sem_signal_safe(semid_global, SEM_MUTEX);
        usleep(100000);  // 100ms
        sem_wait_safe(semid_global, SEM_MUTEX);
    }
    
    // Kładka pusta - ustaw kierunek WEJŚCIE
    *kierunek = 0;
    log_info("[PRZEWODNIK %d] Kładka pusta - ustawiam kierunek: WEJŚCIE", numer_trasy);
    sem_signal_safe(semid_global, SEM_MUTEX);

    int idx_zwiedzajacy = 0;
    
    for (int tura = 0; tura < (liczba_osob + K - 1) / K; tura++) {
        int w_tej_turze = ((tura + 1) * K <= liczba_osob) ? K : (liczba_osob - tura * K);
        
        log_info("[PRZEWODNIK %d] Tura %d: wpuszczam %d osób...", 
                 numer_trasy, tura + 1, w_tej_turze);
        
        // ATOMOWA OPERACJA: Rezerwuj wszystkie miejsca naraz
        for (int j = 0; j < w_tej_turze; j++) {
            sem_wait_safe(semid_global, sem_kladka);
        }
        
        // Aktualizuj licznik kładki PRZED sygnalizowaniem
        sem_wait_safe(semid_global, SEM_MUTEX);
        *licznik += w_tej_turze;
        log_info("[PRZEWODNIK %d] Na kładce: %d/%d osób", numer_trasy, *licznik, K);
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // Teraz sygnalizuj zwiedzającym
        for (int j = 0; j < w_tej_turze; j++) {
            sem_signal_safe(semid_global, sem_wejscie);
            idx_zwiedzajacy++;
        }
        
        // Czekaj na potwierdzenia
        for (int j = 0; j < w_tej_turze; j++) {
            sem_wait_safe(semid_global, SEM_POTWIERDZENIE);
        }
        
        // Tura przechodzi
        usleep(losuj(1000000, 1500000));
        
        // ATOMOWA OPERACJA: Wszyscy schodzą z kładki
        sem_wait_safe(semid_global, SEM_MUTEX);
        *licznik -= w_tej_turze;
        if (numer_trasy == 1) {
            stan_global->trasa1_licznik += w_tej_turze;
        } else {
            stan_global->trasa2_licznik += w_tej_turze;
        }
        log_info("[PRZEWODNIK %d] Tura %d weszła na trasę (na trasie: %d)", 
                 numer_trasy, tura + 1,
                 (numer_trasy == 1) ? stan_global->trasa1_licznik : stan_global->trasa2_licznik);
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // Zwolnij miejsca na kładce
        for (int j = 0; j < w_tej_turze; j++) {
            sem_signal_safe(semid_global, sem_kladka);
        }
    }
    
    log_success("[PRZEWODNIK %d] Wszyscy weszli na trasę", numer_trasy);
}
// WYJŚCIE PRZEZ KŁADKĘ 
void wypusc_przez_kladke(pid_t *grupa_pids, int liczba_osob) {
    (void)grupa_pids;
    
    int sem_kladka = (numer_trasy == 1) ? SEM_KLADKA1 : SEM_KLADKA2;
    int sem_wyjscie = (numer_trasy == 1) ? SEM_GRUPA1_WYJSCIE_KLADKA : SEM_GRUPA2_WYJSCIE_KLADKA;
    
    log_info("[PRZEWODNIK %d] Wypuszczam %d osób z jaskini...", numer_trasy, liczba_osob);

    // SPRAWDŹ I USTAW KIERUNEK KŁADKI
    sem_wait_safe(semid_global, SEM_MUTEX);
    int *kierunek = (numer_trasy == 1) ? &stan_global->kladka1_kierunek : &stan_global->kladka2_kierunek;
    int *licznik = (numer_trasy == 1) ? &stan_global->kladka1_licznik : &stan_global->kladka2_licznik;
    
    // Czekaj aż kładka będzie PUSTA
    while (*licznik > 0) {
        sem_signal_safe(semid_global, SEM_MUTEX);
        usleep(100000);
        sem_wait_safe(semid_global, SEM_MUTEX);
    }
    
    // Kładka pusta - ustaw kierunek WYJŚCIE
    *kierunek = 1;
    log_info("[PRZEWODNIK %d] Kładka pusta - ustawiam kierunek: WYJŚCIE", numer_trasy);
    sem_signal_safe(semid_global, SEM_MUTEX);

    int idx_zwiedzajacy = 0;
    
    for (int tura = 0; tura < (liczba_osob + K - 1) / K; tura++) {
        int w_tej_turze = ((tura + 1) * K <= liczba_osob) ? K : (liczba_osob - tura * K);
        
        // ATOMOWA: Rezerwuj miejsca
        for (int j = 0; j < w_tej_turze; j++) {
            sem_wait_safe(semid_global, sem_kladka);
        }
        
        // ATOMOWA: Przenieś z trasy na kładkę
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->trasa1_licznik -= w_tej_turze;
        } else {
            stan_global->trasa2_licznik -= w_tej_turze;
        }
        *licznik += w_tej_turze;
        log_info("[PRZEWODNIK %d] Tura %d wchodzi na kładkę wyjściową (%d osób)", 
                 numer_trasy, tura + 1, w_tej_turze);
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // Sygnalizuj zwiedzającym
        for (int j = 0; j < w_tej_turze; j++) {
            sem_signal_safe(semid_global, sem_wyjscie);
            idx_zwiedzajacy++;
        }
        
        // Czekaj na potwierdzenia
        for (int j = 0; j < w_tej_turze; j++) {
            sem_wait_safe(semid_global, SEM_POTWIERDZENIE);
        }
        
        // Przejście
        usleep(losuj(1000000, 1500000));
        
        // ATOMOWA: Opuścili kładkę
        sem_wait_safe(semid_global, SEM_MUTEX);
        *licznik -= w_tej_turze;
        log_info("[PRZEWODNIK %d] Tura %d opuściła jaskinię", numer_trasy, tura + 1);
        sem_signal_safe(semid_global, SEM_MUTEX);
        
        // Zwolnij kładkę
        for (int j = 0; j < w_tej_turze; j++) {
            sem_signal_safe(semid_global, sem_kladka);
        }
    }
    
    log_success("[PRZEWODNIK %d] Wszyscy wyszli na zewnątrz", numer_trasy);
}


int main(int argc, char *argv[]) {
    int semid = atoi(getenv("SEMID"));

    if (argc < 2) {
        fprintf(stderr, "Użycie: %s <numer_trasy>\n", argv[0]);
        return 1;
    }
   
    numer_trasy = atoi(argv[1]);
    if (numer_trasy != 1 && numer_trasy != 2) return 1;
   
    log_info("[PRZEWODNIK %d] Startuje (PID: %d)", numer_trasy, getpid());
   
    shmid_global = atoi(getenv("SHMID"));
    semid_global = atoi(getenv("SEMID"));
    stan_global = podlacz_pamiec_dzielona(shmid_global);
   
    if (numer_trasy == 1) stan_global->pid_przewodnik1 = getpid();
    else stan_global->pid_przewodnik2 = getpid();
   
    signal(SIGUSR1, obsluga_zamkniecia);
    signal(SIGUSR2, obsluga_zamkniecia);
   
    int czas_trasy = (numer_trasy == 1) ? T1 : T2;
    int max_na_trasie = (numer_trasy == 1) ? N1 : N2;
    int numer_wycieczki = 0;
    int sem_limit = (numer_trasy == 1) ? SEM_TRASA1_LIMIT : SEM_TRASA2_LIMIT;
   
    while (!flaga_zamkniecie && stan_global->jaskinia_otwarta) {
        int sem_kolejka = (numer_trasy == 1) ? 
        SEM_KOLEJKA1_NIEPUSTA : SEM_KOLEJKA2_NIEPUSTA;
       
        // Blokuj się i czekaj, aż ktoś faktycznie pojawi się w kolejce
        sem_wait_safe(semid_global, sem_kolejka);
       
        // Sprawdzenie po obudzeniu, czy jaskinia nie została zamknięta
        if (flaga_zamkniecie || !stan_global->jaskinia_otwarta) break;

        // ZBIERZ GRUPĘ Z KOLEJKI
        int liczba_w_grupie = zbierz_grupe(numer_trasy, stan_global, semid_global, max_na_trasie);
        
        if (liczba_w_grupie <= 0) {
        continue; 
        }
        
        numer_wycieczki++;
       
        log_info("[PRZEWODNIK %d] ════════════ WYCIECZKA #%d ════════════", numer_trasy, numer_wycieczki);
        
        log_success("[PRZEWODNIK %d] Grupa zebrana: %d osób", numer_trasy, liczba_w_grupie);
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                    "Wycieczka #%d: START (grupa: %d osób)", numer_wycieczki, liczba_w_grupie);

        // SPRAWDZENIE PRZED ROZPOCZĘCIEM
        if (flaga_zamkniecie) {
            log_warning("[PRZEWODNIK %d] Sygnał przed wyjściem - ANULACJA! Grupa się rozchodzi!", numer_trasy);
            
            // LOGUJ ANULOWANIE
            log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                        "Wycieczka #%d: ANULOWANA (zamknięcie przed startem, grupa: %d osób)", 
                        numer_wycieczki, liczba_w_grupie);
            
            // Powiadom zwiedzających że wycieczka odwołana (wyślij sygnały wyjścia = wracają)
            int sem_gotowa = (numer_trasy == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
            for (int i = 0; i < liczba_w_grupie; i++) {
                sem_signal_safe(semid_global, sem_gotowa);  // Odblokuj zwiedzających
            }
            
            sem_wait_safe(semid_global, SEM_MUTEX);
            if (numer_trasy == 1) stan_global->trasa1_czeka_na_grupe = 0;
            else stan_global->trasa2_czeka_na_grupe = 0;
            sem_signal_safe(semid_global, SEM_MUTEX);
            
            log_info("[PRZEWODNIK %d] Zwolniono %d zwiedzających z grupy", numer_trasy, liczba_w_grupie);
            
            goto koniec;
        }

        // Oznacz że przewodnik czeka na grupę
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->trasa1_czeka_na_grupe = 1;
            stan_global->trasa1_wycieczka_nr = numer_wycieczki;
        } else {
            stan_global->trasa2_czeka_na_grupe = 1;
            stan_global->trasa2_wycieczka_nr = numer_wycieczki;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);

        // REZERWACJA MIEJSC NA TRASIE (semafory limitów)
        log_info("[PRZEWODNIK %d] Rezerwuję %d miejsc na trasie...", numer_trasy, liczba_w_grupie);
        for (int i = 0; i < liczba_w_grupie; i++) {
            sem_wait_safe(semid_global, sem_limit);
        }
        log_success("[PRZEWODNIK %d] Miejsca zarezerwowane", numer_trasy);
        
        // POWIADOM ZWIEDZAJĄCYCH ŻE GRUPA UTWORZONA
        int sem_gotowa = (numer_trasy == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
        for (int i = 0; i < liczba_w_grupie; i++) {
            sem_signal_safe(semid_global, sem_gotowa);
        }
        log_info("[PRZEWODNIK %d] Powiadomiłem zwiedzających, że grupa została utworzona.", numer_trasy);
        
        // Pobierz tablicę PID-ów grupy
        pid_t *grupa_pids = (numer_trasy == 1) ? stan_global->grupa1_pids : stan_global->grupa2_pids;
        
        // WPUSZCZANIE PRZEZ KŁADKĘ
        wpusc_przez_kladke(grupa_pids, liczba_w_grupie);
       
        // ROZPOCZĘCIE WYCIECZKI
        w_trakcie_wycieczki = 1;
        
        log_info("[PRZEWODNIK %d] Trasa w toku (czas: %ds)...", numer_trasy, czas_trasy);
       
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) stan_global->grupa1_aktywna = 1;
        else stan_global->grupa2_aktywna = 1;
        sem_signal_safe(semid_global, SEM_MUTEX);
       
        // ZWIEDZANIE - jeśli sygnał przyjdzie tutaj, wycieczki dokończone
        sleep(czas_trasy);
        
        // KONIEC ZWIEDZANIA
        w_trakcie_wycieczki = 0;
       
        // POWRÓT PRZEZ KŁADKĘ
        log_info("[PRZEWODNIK %d] Powrót grupy...", numer_trasy);
        wypusc_przez_kladke(grupa_pids, liczba_w_grupie);
       
        // ZWOLNIENIE MIEJSC
        for (int i = 0; i < liczba_w_grupie; i++) {
            sem_signal_safe(semid_global, sem_limit);
        }
        log_info("[PRZEWODNIK %d] Zwolniono semafory trasy (%d miejsc)", numer_trasy, liczba_w_grupie);
       
        sem_wait_safe(semid_global, SEM_MUTEX);
        if (numer_trasy == 1) {
            stan_global->grupa1_aktywna = 0;
            stan_global->trasa1_czeka_na_grupe = 0;
            for (int i = 0; i < liczba_w_grupie; i++) {
                stan_global->grupa1_pids[i] = 0;
            }
            stan_global->grupa1_liczba = 0;
        } else {
            stan_global->grupa2_aktywna = 0;
            stan_global->trasa2_czeka_na_grupe = 0;
            for (int i = 0; i < liczba_w_grupie; i++) {
                stan_global->grupa2_pids[i] = 0;
            }
            stan_global->grupa2_liczba = 0;
        }
        sem_signal_safe(semid_global, SEM_MUTEX);
       
        log_to_file((numer_trasy == 1) ? LOG_TRASA1 : LOG_TRASA2,
                    "Wycieczka #%d: KONIEC", numer_wycieczki);
       
        log_success("[PRZEWODNIK %d] Wycieczka #%d zakończona sukcesem", numer_trasy, numer_wycieczki);
       
        // SPRAWDZENIE PO ZAKOŃCZENIU
        if (flaga_zamkniecie) {
            log_info("[PRZEWODNIK %d] Sygnał po zakończeniu wycieczki - kończę pracę", numer_trasy);
            goto koniec;
        }
        
        sleep(1);
    }
   
koniec:
    log_info("[PRZEWODNIK %d] Zakończono prowadzenie wycieczek", numer_trasy);
    log_success("[PRZEWODNIK %d] Łącznie przeprowadzono: %d wycieczek", numer_trasy, numer_wycieczki);
   
    sem_wait_safe(semid_global, SEM_MUTEX);
    int czekajacych = (numer_trasy == 1) ? 
        stan_global->kolejka_trasa1_koniec : 
        stan_global->kolejka_trasa2_koniec;
    
    if (czekajacych > 0) {
        log_info("[PRZEWODNIK %d] %d zwiedzających czeka w kolejce - zostaną poinformowani o zamknięciu", 
                numer_trasy, czekajacych);
        
        // WYCZYŚĆ KOLEJKĘ 
        if (numer_trasy == 1) {
            stan_global->kolejka_trasa1_koniec = 0;
        } else {
            stan_global->kolejka_trasa2_koniec = 0;
        }
    }
    sem_signal_safe(semid_global, SEM_MUTEX);
    
    int sem_gotowa = (numer_trasy == 1) ? SEM_PRZEWODNIK1_READY : SEM_PRZEWODNIK2_READY;
    
    for (int i = 0; i < czekajacych; i++) {
        sem_signal_safe(semid_global, sem_gotowa);
    }
    
    if (czekajacych > 0) {
        log_info("[PRZEWODNIK %d] Poinformowano %d zwiedzających o zamknięciu", numer_trasy, czekajacych);
    }
   
    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║   PODSUMOWANIE PRZEWODNIKA TRASY %d   ║\n", numer_trasy);
    printf("╚═══════════════════════════════════════╝\n");
    sem_wait_safe(semid, SEM_MUTEX);
    printf("Przeprowadzono wycieczek: %d\n", numer_wycieczki);
    printf("Czas trasy:               %d sekund\n", czas_trasy);
    printf("Max osób na trasie:       %d\n", (numer_trasy == 1 ? N1 : N2));
    printf("Max osób na kładce:       %d\n", K);
    sem_signal_safe(semid, SEM_MUTEX);
    printf("\n");
   
    odlacz_pamiec_dzielona(stan_global);
   
    return 0;
}
    
    

