#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <errno.h>

/// Limity tras
#define N1 8
#define N2 8

// Kładki
#define K 3

// Czasy wycieczek (sekundy rzeczywiste w symulacji)
#define T1 8
#define T2 10

// === GODZINY OTWARCIA JASKINI (format 24h) ===
#define TP 10
#define TK 16

// Przyspieszenie symulacji
// 240 = 4 minuty rzeczywiste = 1 sekunda symulacji (8h = 2 minuty)
#define PRZYSPIESZENIE 240
#define CZAS_OTWARCIA_SEK ((TK - TP) * 3600 / PRZYSPIESZENIE)  // Ile sekund symulacji trwa dzień pracy              

// === PARAMETRY SYMULACJI ===
#define CZAS_SYMULACJI CZAS_OTWARCIA_SEK
#define WIEK_DOROSLY 18             // Od tego wieku można być opiekunem

// Opóźnienia (sekundy)
#define GENERATOR_MIN_DELAY 0
#define GENERATOR_MAX_DELAY 1

// Prawdopodobieństwa (%)
#define SZANSA_POWROT 10
#define SZANSA_OPIEKUN_Z_DZIECKIEM 30

// === CENY BILETÓW ===

#define CENA_BAZOWA 30.0f
#define ZNIZKA_POWROT 0.5f
#define ZNIZKA_DZIECKO_Z_OPIEKUNEM 0.2f  // 20% zniżki dla dziecka z opiekunem

// === OGRANICZENIA WIEKOWE ===
#define WIEK_MIN 1
#define WIEK_MAX 80
#define WIEK_GRATIS 3
#define WIEK_TYLKO_TRASA2_DZIECKO 8
#define WIEK_TYLKO_TRASA2_SENIOR 75

// === KOMUNIKATY (typy w kolejce) ===
#define MSG_BILET_POWROT 1      // Powtórka - OMIJA KOLEJKĘ
#define MSG_BILET_ZWYKLY 2    // Normalna kolejka       

// Limit komunikatów w kolejce
#define MAX_MSG_QUEUE 100

// === SEMAFORY (indeksy w zestawie) ===
enum {
    SEM_MUTEX = 0,
    SEM_MUTEX_KLADKA,
    SEM_TRASA1_LIMIT,
    SEM_TRASA2_LIMIT,

    SEM_KLADKA1,        // POJEMNOŚĆ KŁADKI TRASA 1 (init = K)
    SEM_KLADKA2,        // POJEMNOŚĆ KŁADKI TRASA 2 (init = K)

    SEM_KOLEJKA1_NIEPUSTA,
    SEM_KOLEJKA2_NIEPUSTA,

    SEM_PRZEWODNIK1_READY,
    SEM_PRZEWODNIK2_READY,

    SEM_KLADKA1_WEJSCIE_ALLOWED,   // BINARNY
    SEM_KLADKA1_WYJSCIE_ALLOWED,   // BINARNY
    SEM_KLADKA2_WEJSCIE_ALLOWED,
    SEM_KLADKA2_WYJSCIE_ALLOWED,
    SEM_KOLEJKA_MSG_SLOTS,          
    SEM_POTWIERDZENIE_WEJSCIE_TRASA1,
    SEM_POTWIERDZENIE_WYJSCIE_TRASA1,

    SEM_POTWIERDZENIE_WEJSCIE_TRASA2,
    SEM_POTWIERDZENIE_WYJSCIE_TRASA2,
    SEM_ZAKONCZENI,
    SEM_WOLNE_SLOTY_ZWIEDZAJACYCH,

    NUM_SEMS
};

// === ŚCIEŻKI DO PLIKÓW ===

#define LOG_BILETY "logs/bilety.txt"
#define LOG_TRASA1 "logs/trasa1.txt"
#define LOG_TRASA2 "logs/trasa2.txt"
#define LOG_SYMULACJA "logs/symulacja.log"
#define LOG_GENERATOR "logs/generator.txt"

// === KOLORY TERMINALA ===

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// === DEBUGOWANIE ===

#define DEBUG_MODE 1

#if DEBUG_MODE
    #define DEBUG_PRINT(fmt, ...) \
        fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
    #define DEBUG_PRINT(fmt, ...)
#endif

#endif // CONFIG_H