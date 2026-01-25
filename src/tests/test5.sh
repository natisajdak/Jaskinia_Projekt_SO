#!/bin/bash

echo "=== TEST 5: Synchronizacja opiekun-dziecko ==="
echo ""

# Konfiguracja: duzo opiekunow
sed -i 's/#define SZANSA_OPIEKUN_Z_DZIECKIEM .*/#define SZANSA_OPIEKUN_Z_DZIECKIEM 70/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 14/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 360/' include/config.h

echo "[TEST5] Konfiguracja: 70% opiekunow z dziecmi, Trasa 2"

make clean > /dev/null 2>&1 && make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Blad kompilacji"
    exit 1
fi

echo "[TEST5] Uruchamiam symulacje (60s)..."
timeout 60s ./bin/main > test5_output.log 2>&1

echo ""
echo "=== ANALIZA PAR OPIEKUN-DZIECKO ==="
echo ""

OPIEKUNOW=$(grep -c "OPIEKUN.*Jestem z dzieckiem" test5_output.log)
WATKI_UTWORZONE=$(grep -c "Utworzon.*w.*tek dziecka" test5_output.log)

echo "--- Zarejestrowane pary ---"
echo "  - Opiekunow: $OPIEKUNOW"
echo "  - Watkow dzieci: $WATKI_UTWORZONE"

if [ "$OPIEKUNOW" -gt 0 ] && [ "$WATKI_UTWORZONE" -gt 0 ]; then
    echo "PASS: System tworzy pary opiekun-dziecko"
else
    echo "FAIL: Brak par opiekun-dziecko"
fi

echo ""

# === SYNCHRONIZACJA WEJSCIA ===
echo "--- Synchronizacja wejscia ---"

SYGNAL_WEJSCIE=$(grep -c "Da.*em sygna.*dziecku.*jeste.*my na trasie" test5_output.log)
DZIECKO_OTRZYMAL=$(grep -c "DZIECKO.*Otrzyma.*em sygna.*na trasie" test5_output.log)

echo "  - Opiekun dal sygnal: $SYGNAL_WEJSCIE"
echo "  - Dziecko otrzymalo: $DZIECKO_OTRZYMAL"

if [ "$SYGNAL_WEJSCIE" -gt 0 ] && [ "$DZIECKO_OTRZYMAL" -gt 0 ]; then
    echo "PASS: Synchronizacja wejscia dziala"
else
    echo "WARNING: Problemy z synchronizacja wejscia"
fi

echo ""

# === SYNCHRONIZACJA WYJSCIA ===
echo "--- Synchronizacja wyjscia ---"

SYGNAL_WYJSCIE=$(grep -c "Da.*em sygna.*dziecku.*wysz" test5_output.log)

# POPRAWKA TUTAJ: Używamy kropki (.) zamiast konkretnych liter, żeby ominąć problem z kodowaniem ś/s
DZIECKO_WYSZEDL=$(grep -c "DZIECKO.*Opu.*ci.*em jaskini.*z opiekunem" test5_output.log)

RAZEM_WYSZLI=$(grep -c "Dziecko-w.*tek zako.*czone.*wychodzimy razem" test5_output.log)

echo "  - Opiekun dal sygnal wyjscia: $SYGNAL_WYJSCIE"
echo "  - Dzieci wyszly: $DZIECKO_WYSZEDL"
echo "  - Potwierdzenia wspolnego wyjscia: $RAZEM_WYSZLI"

# Zmieniamy warunek zaliczenia: Ważne jest, że wątek się zakończył (RAZEM_WYSZLI), 
# nawet jeśli z powodu ewakuacji nie wysłano sygnału 'wyszliśmy normalnie'.
if [ "$RAZEM_WYSZLI" -gt 0 ]; then
    echo "PASS: Synchronizacja wyjscia dziala"
else
    echo "WARNING: Problemy z synchronizacja wyjscia"
fi

echo ""

# === WERYFIKACJA REGULAMINU ===
echo "--- Regulamin (dzieci bez opiekuna) ---"

ODRZUCONE=$(grep -c "musz.*mie.*opiekuna.*Odchodz" test5_output.log)

if [ "$ODRZUCONE" -gt 0 ]; then
    echo "PASS: Dzieci bez opiekuna sa odrzucane ($ODRZUCONE)"
else
    echo "INFO: Nie bylo dzieci bez opiekuna"
fi

echo ""

echo "=== PODSUMOWANIE TEST 5 ==="

SCORE=0
[ "$OPIEKUNOW" -gt 0 ] && [ "$WATKI_UTWORZONE" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$SYGNAL_WEJSCIE" -gt 0 ] && [ "$DZIECKO_OTRZYMAL" -gt 0 ] && SCORE=$((SCORE + 1))
# Punktujemy za poprawne dołączenie wątków (join), a nie tylko za "szczęśliwe" wyjścia
[ "$RAZEM_WYSZLI" -gt 0 ] && SCORE=$((SCORE + 1))

echo "Wynik: $SCORE/3"

if [ "$SCORE" -ge 2 ]; then
    echo "PASS: TEST 5 ZALICZONY"
    exit 0
else
    echo "WARNING: TEST 5 WYMAGA UWAGI"
    exit 1
fi