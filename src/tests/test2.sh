#!/bin/bash

echo "=== TEST 2: Weryfikacja regulaminu biletowego ==="
echo ""

# Konfiguracja
sed -i 's/#define WIEK_MIN .*/#define WIEK_MIN 1/' include/config.h
sed -i 's/#define WIEK_MAX .*/#define WIEK_MAX 80/' include/config.h
sed -i 's/#define WIEK_TYLKO_TRASA2_DZIECKO .*/#define WIEK_TYLKO_TRASA2_DZIECKO 8/' include/config.h
sed -i 's/#define WIEK_TYLKO_TRASA2_SENIOR .*/#define WIEK_TYLKO_TRASA2_SENIOR 75/' include/config.h
sed -i 's/#define SZANSA_OPIEKUN_Z_DZIECKIEM .*/#define SZANSA_OPIEKUN_Z_DZIECKIEM 50/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 14/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 360/' include/config.h

echo "[TEST2] Kompilacja..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Blad kompilacji"
    exit 1
fi

echo "[TEST2] Uruchamiam symulacje (60s)..."
timeout 60s ./bin/main > test2_output.log 2>&1

echo ""
echo "=== ANALIZA REGULAMINU ==="
echo ""

SCORE=0

echo "--- Scenariusz 1: Dzieci bez opiekuna ---"

DZIECI_ODRZUCONE=$(grep "muszę mieć opiekuna! Odchodzę" test2_output.log 2>/dev/null | wc -l)

# Usuń białe znaki
DZIECI_ODRZUCONE=$(echo "$DZIECI_ODRZUCONE" | tr -d '[:space:]')

echo "  Dzieci odrzucone (komunikat): $DZIECI_ODRZUCONE"

if [ "$DZIECI_ODRZUCONE" -gt 0 ]; then
    echo "  PASS: System odrzuca dzieci <8 lat bez opiekuna"
    SCORE=$((SCORE + 1))
else
    echo "  INFO: Brak odrzuceń (nie wylosowano dzieci bez opiekuna)"
fi

echo ""

echo "--- Scenariusz 2: Seniorzy i ograniczenie tras ---"

SENIORZY_T1=$(grep "Typ=SENIOR" logs/bilety.txt 2>/dev/null | grep "Status=OK" | grep "Trasa=1" | wc -l)
SENIORZY_T2=$(grep "Typ=SENIOR" logs/bilety.txt 2>/dev/null | grep "Status=OK" | grep "Trasa=2" | wc -l)

# Usuń białe znaki
SENIORZY_T1=$(echo "$SENIORZY_T1" | tr -d '[:space:]')
SENIORZY_T2=$(echo "$SENIORZY_T2" | tr -d '[:space:]')

echo "  Seniorzy na Trasie 1:         $SENIORZY_T1"
echo "  Seniorzy na Trasie 2:         $SENIORZY_T2"

if [ "$SENIORZY_T1" -eq 0 ]; then
    if [ "$SENIORZY_T2" -gt 0 ]; then
        echo "  PASS: Żaden senior nie dostał się na Trasę 1"
        SCORE=$((SCORE + 1))
    else
        echo "  INFO: Brak seniorów w symulacji"
    fi
else
    echo "  FAIL: Wykryto $SENIORZY_T1 seniorów na Trasie 1!"
fi

echo ""

echo "--- Scenariusz 3: Opiekunowie z dziećmi ---"

OPIEKUNOWIE_OK=$(grep "Typ=OPIEKUN_Z_DZIECKIEM" logs/bilety.txt 2>/dev/null | grep "Status=OK" | wc -l)
OPIEKUNOWIE_T1=$(grep "Typ=OPIEKUN_Z_DZIECKIEM" logs/bilety.txt 2>/dev/null | grep "Status=OK" | grep "Trasa=1" | wc -l)
OPIEKUNOWIE_T2=$(grep "Typ=OPIEKUN_Z_DZIECKIEM" logs/bilety.txt 2>/dev/null | grep "Status=OK" | grep "Trasa=2" | wc -l)

# Usuń białe znaki
OPIEKUNOWIE_OK=$(echo "$OPIEKUNOWIE_OK" | tr -d '[:space:]')
OPIEKUNOWIE_T1=$(echo "$OPIEKUNOWIE_T1" | tr -d '[:space:]')
OPIEKUNOWIE_T2=$(echo "$OPIEKUNOWIE_T2" | tr -d '[:space:]')

echo "  Opiekunowie zaakceptowani:    $OPIEKUNOWIE_OK"
echo "  Na Trasie 1:                  $OPIEKUNOWIE_T1"
echo "  Na Trasie 2:                  $OPIEKUNOWIE_T2"

if [ "$OPIEKUNOWIE_OK" -gt 0 ]; then
    if [ "$OPIEKUNOWIE_T1" -eq 0 ] && [ "$OPIEKUNOWIE_T2" -eq "$OPIEKUNOWIE_OK" ]; then
        echo "  PASS: Wszyscy opiekunowie poprawnie na Trasie 2"
        SCORE=$((SCORE + 1))   
    else
        echo "  FAIL: Opiekun na złej trasie!"
    fi
else
    echo "  INFO: Brak opiekunów w symulacji"
fi

echo ""


echo "--- Weryfikacja: Ceny biletów opiekuna z dzieckiem---"

CENY_OPIEKUN=$(grep "Typ=OPIEKUN_Z_DZIECKIEM" logs/bilety.txt 2>/dev/null | grep "Status=OK" | grep -oP "Cena=\K[0-9.]+" | head -1)

if [ -n "$CENY_OPIEKUN" ]; then 
    if command -v bc &> /dev/null; then
        if (( $(echo "$CENY_OPIEKUN > 30.0" | bc -l) )); then
            echo "  INFO: Cena poprawna (opiekun + dziecko)"
        fi
    fi
fi

echo ""

echo "=== PODSUMOWANIE TEST 3 ==="
# Statystyki
TOTAL_OK=$(grep "Status=OK" logs/bilety.txt 2>/dev/null | wc -l | tr -d '[:space:]')
TOTAL_ODMOWA=$(grep "Status=ODMOWA" logs/bilety.txt 2>/dev/null | wc -l | tr -d '[:space:]')

echo "Statystyki z logs/bilety.txt:"
echo "  - Bilety wydane:     $TOTAL_OK"
echo "  - Odmowy kasjera:    $TOTAL_ODMOWA"
echo "  - Opiekunowie:       $OPIEKUNOWIE_OK"
echo "  - Seniorzy (T2):     $SENIORZY_T2"

echo ""

# Decyzja
if [ "$SCORE" -ge 2 ]; then
    echo " TEST 2 ZALICZONY"
    echo ""
    echo "System poprawnie egzekwuje regulamin biletowy!"
    exit 0
else
    echo " TEST 2 NIEZALICZONY "
    echo ""
    echo "Przyczyna: Zbyt mało scenariuszy zaliczonych ($SCORE/3)"
    echo ""
    echo "Sprawdź logi:"
    echo "  - test2_output.log (komunikaty procesów)"
    echo "  - logs/bilety.txt (historia sprzedaży)"
    exit 1
fi