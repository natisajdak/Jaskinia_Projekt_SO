#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Zapis do pliku z timestamp
void log_to_file(const char *filename, const char *format, ...);

// Log do konsoli z kolorem
void log_info(const char *format, ...);
void log_success(const char *format, ...);
void log_warning(const char *format, ...);
void log_error(const char *format, ...);

// === CZAS ===

// Zwraca czas od startu symulacji (sekundy)
int czas_od_startu(time_t start);

// Formatuj timestamp do stringa
void format_timestamp(char *buf, size_t size, time_t t);

// === LOSOWANIE ===

// Losowa liczba z zakresu [min, max]
int losuj(int min, int max);

// Losowa szansa (%) - zwraca 1 z prawdopodobieństwem prob%
int losuj_szanse(int prob);

// === WALIDACJA ===

// Sprawdź czy zwiedzający może wejść na daną trasę
int waliduj_trase(int wiek, int trasa);

// Oblicz cenę biletu
float oblicz_cene(int wiek, int czy_powrot);

int sprawdz_regulamin(int wiek, int trasa, int ma_opiekuna); 
const char* pobierz_powod_odmowy(void);

// Konfiguracja obsługi sygnałów
void konfiguruj_sygnaly(void);

#endif // UTILS_H
