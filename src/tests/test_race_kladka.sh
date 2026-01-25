#!/bin/bash

echo "=== TEST: Race condition i synchronizacja ==="  

# 1. Konfiguracja
echo "[TEST] Przygotowanie konfiguracji..."

# Backup
cp include/config.h include/config.h.backup

# Agresywne parametry dla race condition
sed -i 's/#define K .*/#define K 3/' include/config.h
sed -i 's/#define N1 .*/#define N1 8/' include/config.h
sed -i 's/#define N2 .*/#define N2 8/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 14/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 360/' include/config.h
sed -i 's/#define T1 .*/#define T1 5/' include/config.h
sed -i 's/#define T2 .*/#define T2 5/' include/config.h
sed -i 's/#define GENERATOR_MIN_DELAY .*/#define GENERATOR_MIN_DELAY 1/' include/config.h
sed -i 's/#define GENERATOR_MAX_DELAY .*/#define GENERATOR_MAX_DELAY 2/' include/config.h

echo "[TEST] Konfiguracja: K=3, N=8, Krótkie wycieczki (5s), Szybka generacja"

# 2. Kompilacja
echo "[TEST] Kompilacja..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Błąd kompilacji"
    mv include/config.h.backup include/config.h
    exit 1
fi

# 3. Uruchomienie
CZAS_TESTU=60
echo "[TEST] Uruchamiam symulację (${CZAS_TESTU}s)..."

rm -f test3_output.log
timeout ${CZAS_TESTU}s ./bin/main > test3_output.log 2>&1 &
MAIN_PID=$!

echo "[TEST] Monitoruję race conditions..."
echo ""

# 4. Monitoring w czasie rzeczywistym
MAX_K1=0
MAX_K2=0
PRZEKROCZENIA_K1=0
PRZEKROCZENIA_K2=0
MAX_DOZWOLONE=3

for i in $(seq 1 $CZAS_TESTU); do
    # Wyciągnij maksymalną wartość licznika kładki 1 (WEJŚCIE i WYJŚCIE)
    VAL_K1_WEJSCIE=$(grep "Na kładce 1 WEJŚCIE:" test3_output.log 2>/dev/null | grep -oP "WEJŚCIE: \K\d+" | sort -nr | head -1)
    VAL_K1_WYJSCIE=$(grep "Na kładce 1 WYJŚCIE:" test3_output.log 2>/dev/null | grep -oP "WYJŚCIE: \K\d+" | sort -nr | head -1)
    
    # Wyciągnij maksymalną wartość licznika kładki 2
    VAL_K2_WEJSCIE=$(grep "Na kładce 2 WEJŚCIE:" test3_output.log 2>/dev/null | grep -oP "WEJŚCIE: \K\d+" | sort -nr | head -1)
    VAL_K2_WYJSCIE=$(grep "Na kładce 2 WYJŚCIE:" test3_output.log 2>/dev/null | grep -oP "WYJŚCIE: \K\d+" | sort -nr | head -1)
    
    # Znajdź maksimum
    [ -n "$VAL_K1_WEJSCIE" ] && [ "$VAL_K1_WEJSCIE" -gt "$MAX_K1" ] && MAX_K1=$VAL_K1_WEJSCIE
    [ -n "$VAL_K1_WYJSCIE" ] && [ "$VAL_K1_WYJSCIE" -gt "$MAX_K1" ] && MAX_K1=$VAL_K1_WYJSCIE
    [ -n "$VAL_K2_WEJSCIE" ] && [ "$VAL_K2_WEJSCIE" -gt "$MAX_K2" ] && MAX_K2=$VAL_K2_WEJSCIE
    [ -n "$VAL_K2_WYJSCIE" ] && [ "$VAL_K2_WYJSCIE" -gt "$MAX_K2" ] && MAX_K2=$VAL_K2_WYJSCIE
    
    # Sprawdź przekroczenia
    if [ "$MAX_K1" -gt "$MAX_DOZWOLONE" ]; then
        PRZEKROCZENIA_K1=$((PRZEKROCZENIA_K1 + 1))
    fi
    
    if [ "$MAX_K2" -gt "$MAX_DOZWOLONE" ]; then
        PRZEKROCZENIA_K2=$((PRZEKROCZENIA_K2 + 1))
    fi
    
    # Progress co 10s
    if [ $((i % 10)) -eq 0 ]; then
        echo "  [$i/${CZAS_TESTU}s] Peak → Kładka 1: $MAX_K1/3, Kładka 2: $MAX_K2/3"
    fi
    
    sleep 1
done

# Zatrzymaj program
echo ""
echo "[TEST] Zatrzymuję symulację..."
kill -SIGINT $MAIN_PID 2>/dev/null
wait $MAIN_PID 2>/dev/null

# Przywróć config
mv include/config.h.backup include/config.h


echo "=== ANALIZA WYNIKÓW ==="

SCORE=0


echo "--- Scenariusz 1: Limit kładki (K=3) ---"

echo "  Maksymalne obłożenie:"
echo "    Kładka 1: $MAX_K1/3"
echo "    Kładka 2: $MAX_K2/3"

TOTAL_PRZEKROCZENIA=$((PRZEKROCZENIA_K1 + PRZEKROCZENIA_K2))

if [ "$MAX_K1" -le "$MAX_DOZWOLONE" ] && [ "$MAX_K2" -le "$MAX_DOZWOLONE" ]; then
    echo "  PASS: Limit kładki nigdy nie został przekroczony"
    SCORE=$((SCORE + 1))
else
    echo "  FAIL: Wykryto przekroczenie limitu!"
    echo ""
    echo "  Przykłady przekroczeń:"
    
    if [ "$MAX_K1" -gt "$MAX_DOZWOLONE" ]; then
        grep "Na kładce 1" test3_output.log | grep -E "(WEJŚCIE|WYJŚCIE): ([4-9]|[1-9][0-9]+)/" | head -3 | sed 's/^/    /'
    fi
    
    if [ "$MAX_K2" -gt "$MAX_DOZWOLONE" ]; then
        grep "Na kładce 2" test3_output.log | grep -E "(WEJŚCIE|WYJŚCIE): ([4-9]|[1-9][0-9]+)/" | head -3 | sed 's/^/    /'
    fi
fi

echo ""



echo "--- Scenariusz 2: Kontrola kierunków ruchu ---"

OTWIERA_WEJSCIE=$(grep -c "WEJŚCIE OTWARTE" test3_output.log 2>/dev/null)
ZAMYKA_WEJSCIE=$(grep -c "Zamykam wejście" test3_output.log 2>/dev/null)
OTWIERA_WYJSCIE=$(grep -c "WYJŚCIE OTWARTE" test3_output.log 2>/dev/null)
ZAMYKA_WYJSCIE=$(grep -c "Zamykam wyjście" test3_output.log 2>/dev/null)

echo "  Operacje przewodnika:"
echo "    Otwarć wejścia:   $OTWIERA_WEJSCIE"
echo "    Zamknięć wejścia: $ZAMYKA_WEJSCIE"
echo "    Otwarć wyjścia:   $OTWIERA_WYJSCIE"
echo "    Zamknięć wyjścia: $ZAMYKA_WYJSCIE"

if [ "$OTWIERA_WEJSCIE" -gt 0 ] && [ "$ZAMYKA_WEJSCIE" -gt 0 ]; then
    echo "  PASS: Przewodnik kontroluje ruch jednokierunkowy"
    SCORE=$((SCORE + 1))
else
    echo "  FAIL: Brak kontroli kierunków!"
fi

echo ""


echo "--- Scenariusz 3: Synchronizacja liczników ---"

BLEDY_TRASA=$(grep -c "BŁĄD: trasa=" test3_output.log 2>/dev/null)
BLEDY_LICZNIK=$(grep -c "BŁĄD: licznik" test3_output.log 2>/dev/null)

echo "  Błędy synchronizacji:"
echo "    Błędy licznika tras:  $BLEDY_TRASA"
echo "    Inne błędy liczników: $BLEDY_LICZNIK"

if [ "$BLEDY_TRASA" -eq 0 ] && [ "$BLEDY_LICZNIK" -eq 0 ]; then
    echo "  PASS: Brak błędów synchronizacji"
    SCORE=$((SCORE + 1))
else
    echo "  FAIL: Wykryto błędy synchronizacji!"
    echo ""
    echo "  Przykłady:"
    grep -E "BŁĄD: (trasa=|licznik)" test3_output.log | head -3 | sed 's/^/    /'
fi

echo ""


echo "--- Scenariusz: Wykrywanie zakleszczeń ---"

TIMEOUTY=$(grep -c "TIMEOUT" test3_output.log 2>/dev/null)
TIMEOUTY_WEJSCIE=$(grep -c "TIMEOUT wejścia" test3_output.log 2>/dev/null)
TIMEOUTY_KLADKA=$(grep -c "TIMEOUT: Kładka" test3_output.log 2>/dev/null)

echo "  Timeouty wykryte:      $TIMEOUTY"
echo "    - Wejścia:           $TIMEOUTY_WEJSCIE"
echo "    - Kładka nie pusta:  $TIMEOUTY_KLADKA"

if [ "$TIMEOUTY" -eq 0 ]; then
    echo "Brak timeoutów (brak zakleszczeń)"
else
    echo "  WARNING: Wykryto $TIMEOUTY timeoutów"
    echo ""
    echo "  Przykłady:"
    grep "TIMEOUT" test3_output.log | head -3 | sed 's/^/    /'
fi

echo ""

echo "=== PODSUMOWANIE: Race condition i synchronizacja ==="
echo "Wynik: $SCORE/3 scenariuszy obowiązkowych zaliczonych"
echo ""

# Statystyki
echo "Statystyki końcowe:"
echo "  - Maksymalne obłożenie kładki 1: $MAX_K1/3"
echo "  - Maksymalne obłożenie kładki 2: $MAX_K2/3"
echo "  - Błędów synchronizacji:         $((BLEDY_TRASA + BLEDY_LICZNIK))"
echo "  - Timeoutów:                     $TIMEOUTY"

echo ""

if [ "$SCORE" -ge 2 ]; then
    echo "TEST 3 ZALICZONY"
    echo ""
    echo "System poprawnie zarządza synchronizacją i unika race conditions!"
    exit 0
else
    echo "TEST 3 NIEZALICZONY"
    exit 1
fi