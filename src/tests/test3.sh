#!/bin/bash

echo "=== TEST 3: Limity pojemności tras (N1, N2) ==="
echo ""

# Konfiguracja: małe limity
sed -i 's/#define K .*/#define K 3/' include/config.h
sed -i 's/#define N1 .*/#define N1 5/' include/config.h
sed -i 's/#define N2 .*/#define N2 7/' include/config.h
sed -i 's/#define T1 .*/#define T1 10/' include/config.h
sed -i 's/#define T2 .*/#define T2 12/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 16/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 480/' include/config.h

echo "[TEST3] Konfiguracja: N1=5, N2=7, K=3"

make clean > /dev/null 2>&1 && make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Błąd kompilacji"
    exit 1
fi

echo "[TEST3] Uruchamiam symulację (70s)..."
timeout 70s ./bin/main > test3_output.log 2>&1

echo ""
echo "=== ANALIZA LIMITÓW TRAS ==="
echo ""

# === WYCIĄGNIJ MAKSYMALNE LICZNIKI ===
echo "--- Sprawdzanie maksymalnych obciążeń ---"

# Dla Trasy 1
MAX_T1=$(grep -oP "Wszedłem na trasę 1 \(na trasie: \K[0-9]+" test3_output.log 2>/dev/null | sort -n | tail -1)
[ -z "$MAX_T1" ] && MAX_T1=0

# Dla Trasy 2
MAX_T2=$(grep -oP "Wszedłem na trasę 2 \(na trasie: \K[0-9]+" test3_output.log 2>/dev/null | sort -n | tail -1)
[ -z "$MAX_T2" ] && MAX_T2=0

echo "Maksymalna liczba osób jednocześnie na trasie:"
echo "  - Trasa 1: $MAX_T1 / 5 (limit N1)"
echo "  - Trasa 2: $MAX_T2 / 7 (limit N2)"
echo ""

# === WERYFIKACJA LIMITÓW ===
PASS_T1=0
PASS_T2=0

if [ "$MAX_T1" -le 5 ]; then
    echo "PASS: Trasa 1 nie przekroczyła limitu (max: $MAX_T1)"
    PASS_T1=1
else
    echo "FAIL: Trasa 1 PRZEKROCZYŁA limit! (max: $MAX_T1, limit: 5)"
fi

if [ "$MAX_T2" -le 7 ]; then
    echo "PASS: Trasa 2 nie przekroczyła limitu (max: $MAX_T2)"
    PASS_T2=1
else
    echo "FAIL: Trasa 2 PRZEKROCZYŁA limit! (max: $MAX_T2, limit: 7)"
fi

echo ""

# === SPRAWDŹ CZY BYŁY WYCIECZKI ===
WYCIECZKI_T1=$(grep -c "PRZEWODNIK 1.*Wycieczka #" test3_output.log)
WYCIECZKI_T2=$(grep -c "PRZEWODNIK 2.*Wycieczka #" test3_output.log)

echo "--- Przeprowadzone wycieczki ---"
echo "  - Trasa 1: $WYCIECZKI_T1"
echo "  - Trasa 2: $WYCIECZKI_T2"

if [ "$WYCIECZKI_T1" -eq 0 ] && [ "$WYCIECZKI_T2" -eq 0 ]; then
    echo "WARNING: Brak wycieczek - test niejednoznaczny"
fi

echo ""

# === SPRAWDŹ MECHANIZM OCZEKIWANIA ===
CZEKANIE=$(grep -c "Czekam.*kolejce\|Czekam.*przewodnik" test3_output.log)

echo "--- Mechanizmy synchronizacji ---"
echo "  - Oczekiwań w kolejce: $CZEKANIE"

if [ "$CZEKANIE" -gt 0 ]; then
    echo "PASS: Mechanizmy synchronizacji działają"
fi

echo ""

echo "=== PODSUMOWANIE TEST 3 ==="

if [ $PASS_T1 -eq 1 ] && [ $PASS_T2 -eq 1 ]; then
    if [ "$MAX_T1" -gt 0 ] || [ "$MAX_T2" -gt 0 ]; then
        echo "TEST 3 ZALICZONY"
        echo "   Limity tras działają poprawnie!"
        exit 0
    else
        echo "TEST 3 NIEJEDNOZNACZNY (brak danych)"
        exit 1
    fi
else
    echo "TEST 3 NIEZALICZONY - Limity zostały przekroczone!"
    exit 1
fi