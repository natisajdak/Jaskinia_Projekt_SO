#!/bin/bash

echo "=== TEST 6: Powtorki z priorytetem ==="
echo ""

sed -i 's/#define SZANSA_POWROT .*/#define SZANSA_POWROT 80/' include/config.h
sed -i 's/#define SZANSA_OPIEKUN_Z_DZIECKIEM .*/#define SZANSA_OPIEKUN_Z_DZIECKIEM 5/' include/config.h
sed -i 's/#define T1 .*/#define T1 4/' include/config.h
sed -i 's/#define T2 .*/#define T2 5/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 16/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 300/' include/config.h
sed -i 's/#define N1 .*/#define N1 6/' include/config.h
sed -i 's/#define N2 .*/#define N2 6/' include/config.h

echo "[TEST6] Konfiguracja:"
echo "  - SZANSA_POWROT = 80%"
echo "  - SZANSA_OPIEKUN = 5%"
echo "  - Szybkie wycieczki: T1=4s, T2=5s"

make clean > /dev/null 2>&1 && make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Blad kompilacji"
    exit 1
fi

echo "[TEST6] Uruchamiam symulacje (100s)..."
timeout 100s ./bin/main > test6_output.log 2>&1

echo ""
echo "=== ANALIZA POWTOREK ==="
echo ""

# Policz TYLKO faktyczne powtorki
POWTORKI_LOG=$(grep -c "POWT.*RKA!" test6_output.log)

echo "Faktyczne powtorki w logu: $POWTORKI_LOG"

if [ "$POWTORKI_LOG" -gt 0 ]; then
    echo "PASS: System generuje powtorki!"
else
    echo "FAIL: Brak powtorek mimo 80% szansy!"
    echo "Sprawdzam czy mechanizm dziala..."
fi

echo ""

# Sprawdz bilety
if [ -f "logs/bilety.txt" ]; then
    POWTORKI_BILETY=$(grep -c "Powt.*rka=1" logs/bilety.txt)
    WSZYSTKIE=$(grep -c "Status=OK" logs/bilety.txt)
    
    echo "Bilety:"
    echo "  - Wszystkie: $WSZYSTKIE"
    echo "  - Powtorki: $POWTORKI_BILETY"
    
    if [ "$POWTORKI_BILETY" -gt 0 ]; then
        echo "PASS: Bilety powtorek sa rejestrowane!"
        
        # Sprawdz znizke
        CENA_15=$(grep "Powt.*rka=1" logs/bilety.txt | grep -c "Cena=15\.00")
        if [ "$CENA_15" -gt 0 ]; then
            echo "PASS: Znizka 50% dziala! (cena 15.00 zl)"
        fi
    else
        echo "FAIL: Brak biletow powtorek w logach!"
    fi
fi

echo ""
echo "=== PODSUMOWANIE ==="

SCORE=0
[ "$POWTORKI_LOG" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$POWTORKI_BILETY" -gt 0 ] && SCORE=$((SCORE + 1))

if [ "$SCORE" -eq 2 ]; then
    echo "PASS: TEST 6 ZALICZONY - Powtorki dzialaja!"
    exit 0
elif [ "$SCORE" -eq 1 ]; then
    echo "WARNING: Czesciowo - sprawdz implementacje"
    exit 1
else
    echo "FAIL: Mechanizm powtorek nie dziala!"
    exit 1
fi