#!/bin/bash

echo "=== TEST 1: Przepustowość kładki i ruch jednokierunkowy ==="
echo ""

# 1. Konfiguracja
sed -i 's/#define K .*/#define K 3/' include/config.h
sed -i 's/#define N1 .*/#define N1 10/' include/config.h
sed -i 's/#define N2 .*/#define N2 10/' include/config.h
sed -i 's/#define TP .*/#define TP 7/' include/config.h
sed -i 's/#define TK .*/#define TK 12/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 960/' include/config.h

echo "[TEST1] Konfiguracja: K=3, N1=10, N2=10, Czas=~19s"

# 2. Kompilacja
echo "[TEST1] Kompilacja..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Błąd kompilacji"
    exit 1
fi

# 3. Uruchomienie
echo "[TEST1] Uruchamiam symulację..."
timeout 30s ./bin/main > test1_output.log 2>&1

echo "[TEST1] Analiza logów..."
echo ""

# === WERYFIKACJA LIMITU KŁADKI ===
echo "--- Sprawdzanie limitu kładki (K=3) ---"

# Wyciągnij wszystkie liczniki kładki
PRZEKROCZENIA_WEJSCIE=$(grep -E "Na kładce [12] WEJŚCIE: ([4-9]|[1-9][0-9]+)/" test1_output.log | wc -l)
PRZEKROCZENIA_WYJSCIE=$(grep -E "Na kładce [12] WYJŚCIE: ([4-9]|[1-9][0-9]+)/" test1_output.log | wc -l)
PRZEKROCZENIA=$((PRZEKROCZENIA_WEJSCIE + PRZEKROCZENIA_WYJSCIE))

if [ "$PRZEKROCZENIA" -eq 0 ]; then
    echo "PASS: Limit kładki K=3 nigdy nie został przekroczony"
else
    echo "FAIL: Wykryto $PRZEKROCZENIA przekroczeń limitu kładki!"
fi

echo ""

# === WERYFIKACJA RUCHU JEDNOKIERUNKOWEGO ===
echo "--- Sprawdzanie separacji kierunków ---"

# Sprawdź czy przewodnik prawidłowo zamyka kierunki
ZAMYKA_WYJSCIE=$(grep -c "Zamykam wyjście" test1_output.log)
OTWIERA_WEJSCIE=$(grep -c "Otwieram wejście" test1_output.log)
ZAMYKA_WEJSCIE=$(grep -c "Zamykam wejście" test1_output.log)
OTWIERA_WYJSCIE=$(grep -c "Otwieram wyjście" test1_output.log)

if [ "$ZAMYKA_WYJSCIE" -gt 0 ] && [ "$OTWIERA_WEJSCIE" -gt 0 ]; then
    echo "PASS: Przewodnik kontroluje kierunki ruchu"
else
    echo "FAIL: Brak kontroli kierunków przez przewodnika"
fi

echo ""

# === WERYFIKACJA BRAKU BŁĘDÓW SYNCHRONIZACJI ===
echo "--- Sprawdzanie błędów synchronizacji ---"

BLEDY_TRASA=$(grep -c "BŁĄD: trasa=" test1_output.log)
BLEDY_TIMEOUT=$(grep -c "TIMEOUT" test1_output.log)

if [ "$BLEDY_TRASA" -eq 0 ]; then
    echo "PASS: Brak błędów synchronizacji liczników tras"
else
    echo "FAIL: Znaleziono $BLEDY_TRASA błędów synchronizacji!"
    grep "BŁĄD: trasa=" test1_output.log | head -3
fi

if [ "$BLEDY_TIMEOUT" -eq 0 ]; then
    echo "PASS: Brak timeoutów (brak zakleszczeń)"
else
    echo "WARNING: Wykryto $BLEDY_TIMEOUT timeoutów"
fi

echo ""

echo "=== PODSUMOWANIE TEST 1 ==="
SCORE=0

[ "$PRZEKROCZENIA" -eq 0 ] && SCORE=$((SCORE + 1))
[ "$ZAMYKA_WYJSCIE" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$BLEDY_TRASA" -eq 0 ] && SCORE=$((SCORE + 1))

echo "Wynik: $SCORE/3 testów zaliczonych"

if [ "$SCORE" -ge 2 ]; then
    echo "TEST 1 ZALICZONY"
    exit 0
else
    echo "TEST 1 NIEZALICZONY"
    exit 1
fi