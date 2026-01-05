#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../include/utils.h"
#include "../include/config.h"

// === LOGOWANIE ===
void log_to_file(const char *filename, const char *format, ...) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        perror("open log file");
        return;
    }
    
    // Timestamp
    time_t now = time(NULL);
    char timestamp[64];
    format_timestamp(timestamp, sizeof(timestamp), now);
    
    // Buffer dla wiadomości
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // Zapisz: [timestamp] message\n
    char line[600];
    snprintf(line, sizeof(line), "[%s] %s\n", timestamp, message);
    
    write(fd, line, strlen(line));
    close(fd);
}

void log_info(const char *format, ...) {
    printf(COLOR_CYAN "[INFO] " COLOR_RESET);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_success(const char *format, ...) {
    printf(COLOR_GREEN "[OK] " COLOR_RESET);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_warning(const char *format, ...) {
    printf(COLOR_YELLOW "[WARNING] " COLOR_RESET);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
    fflush(stdout);
}

void log_error(const char *format, ...) {
    fprintf(stderr, COLOR_RED "[ERROR] " COLOR_RESET);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

// === CZAS ===
int czas_od_startu(time_t start) {
    return (int)difftime(time(NULL), start);
}

void format_timestamp(char *buf, size_t size, time_t t) {
    struct tm *tm_info = localtime(&t);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// === LOSOWANIE ===
int losuj(int min, int max) {
    return min + rand() % (max - min + 1);
}

int losuj_szanse(int prob) {
    return (rand() % 100) < prob;
}

// === WALIDACJA ===
int waliduj_trase(int wiek, int trasa) {
    // Dzieci <8 lat: tylko trasa 2
    if (wiek < WIEK_TYLKO_TRASA2_DZIECKO && trasa != 2) {
        return 0;  // Niedozwolone
    }
    
    // Seniorzy >75 lat: tylko trasa 2
    if (wiek > WIEK_TYLKO_TRASA2_SENIOR && trasa != 2) {
        return 0;  // Niedozwolone
    }
    
    return 1;  // OK
}

float oblicz_cene(int wiek, int czy_powrot) {
    // Dzieci <3 lat: gratis
    if (wiek < WIEK_GRATIS) {
        return 0.0f;
    }
    
    // Powtórne wejście: 50% zniżka
    if (czy_powrot) {
        return CENA_BAZOWA * ZNIZKA_POWROT;
    }

    else if (wiek > WIEK_TYLKO_TRASA2_SENIOR) {
        return CENA_BAZOWA * 0.7f; // 30% zniżki seniorzy
    }
    
    // Normalny bilet
    return CENA_BAZOWA;
}

static char ostatni_powod[256] = "";

int sprawdz_regulamin(int wiek, int trasa, int ma_opiekuna) {
    // Dzieci <8 lat bez opiekuna - ODMOWA
    if (wiek < WIEK_TYLKO_TRASA2_DZIECKO && !ma_opiekuna) {
        snprintf(ostatni_powod, sizeof(ostatni_powod),
                 "Dzieci poniżej %d lat wymagają opiekuna", WIEK_TYLKO_TRASA2_DZIECKO);
        return 0;
    }
    
    // Dzieci <8 lat: tylko trasa 2
    if (wiek < WIEK_TYLKO_TRASA2_DZIECKO && trasa != 2) {
        snprintf(ostatni_powod, sizeof(ostatni_powod),
                 "Dzieci poniżej %d lat mogą zwiedzać tylko trasę 2", WIEK_TYLKO_TRASA2_DZIECKO);
        return 0;
    }
    
    // Seniorzy >75 lat: tylko trasa 2
    if (wiek > WIEK_TYLKO_TRASA2_SENIOR && trasa != 2) {
        snprintf(ostatni_powod, sizeof(ostatni_powod),
                 "Osoby powyżej %d lat mogą zwiedzać tylko trasę 2", WIEK_TYLKO_TRASA2_SENIOR);
        return 0;
    }
    
    return 1;  // OK
}

const char* pobierz_powod_odmowy(void) {
    return ostatni_powod;
}


// === SYGNAŁY ===
void konfiguruj_sygnaly(void) {
    signal(SIGPIPE, SIG_IGN);
}


