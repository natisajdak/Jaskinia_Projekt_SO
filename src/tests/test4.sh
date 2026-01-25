#!/bin/bash

echo "=== TEST 4: Sytuacje awaryjne - Sygnały od Strażnika ==="
echo ""

# Konfiguracja
sed -i 's/#define T1 .*/#define T1 10/' include/config.h
sed -i 's/#define T2 .*/#define T2 12/' include/config.h
sed -i 's/#define TP .*/#define TP 8/' include/config.h
sed -i 's/#define TK .*/#define TK 13/' include/config.h
sed -i 's/#define PRZYSPIESZENIE .*/#define PRZYSPIESZENIE 180/' include/config.h

echo "[TEST4] Konfiguracja: T1=10s, T2=12s"

make clean > /dev/null 2>&1 && make > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "FAIL: Błąd kompilacji"
    exit 1
fi

# Czyszczenie
> test4_output.log

echo "[TEST4] Uruchamiam symulacje w tle..."
./bin/main > test4_output.log 2>&1 &
MAIN_PID=$!

echo "[TEST4] PID glownego procesu: $MAIN_PID"

# Poczekaj az system sie zainicjalizuje
echo "[TEST4] Czekam 3s na inicjalizacje..."
sleep 3

# Znajdz PID przewodnikow
echo "[TEST4] Szukam PID przewodnikow..."
sleep 2

# Metoda 1: Szukaj w logach (jezeli przewodnik loguje swoj PID)
PID_PRZEWODNIK1=$(grep -m1 "PRZEWODNIK 1.*PID.*[0-9]" test4_output.log | grep -oE "[0-9]{4,6}" | head -1)
PID_PRZEWODNIK2=$(grep -m1 "PRZEWODNIK 2.*PID.*[0-9]" test4_output.log | grep -oE "[0-9]{4,6}" | head -1)

# Metoda 2: Jezeli logi nie maja PID, uzyj pgrep (szukaj procesow potomnych)
if [ -z "$PID_PRZEWODNIK1" ] || [ -z "$PID_PRZEWODNIK2" ]; then
    echo "[TEST4] Nie znaleziono w logach, szukam po nazwie procesu..."
    
    # Znajdz wszystkie procesy przewodnika
    PRZEWODNICY=$(pgrep -P $MAIN_PID przewodnik 2>/dev/null)
    
    if [ -z "$PRZEWODNICY" ]; then
        # Alternatywnie: wszystkie dzieci procesu main
        PRZEWODNICY=$(pgrep -P $MAIN_PID 2>/dev/null | grep -v $MAIN_PID)
    fi
    
    # Przypisz pierwsze dwa PIDs
    PID_PRZEWODNIK1=$(echo "$PRZEWODNICY" | head -1)
    PID_PRZEWODNIK2=$(echo "$PRZEWODNICY" | tail -1)
fi

echo "[TEST4] Znaleziono przewodnikow:"
echo "  - Przewodnik 1: PID $PID_PRZEWODNIK1"
echo "  - Przewodnik 2: PID $PID_PRZEWODNIK2"

# Sprawdz czy znaleziono
if [ -z "$PID_PRZEWODNIK1" ] || [ -z "$PID_PRZEWODNIK2" ]; then
    echo "WARNING: Nie znaleziono PID przewodnikow - wysylam sygnal do wszystkich procesow"
    sleep 5
    
    # SCENARIUSZ 1: Wyslij SIGUSR1 (zamkniecie Trasy 1) w trakcie wycieczki
    echo ""
    echo "=== SCENARIUSZ 1: SIGUSR1 (zamkniecie Trasy 1) ==="
    echo "[TEST4] Wysylam SIGUSR1 do wszystkich procesow potomnych..."
    pkill -SIGUSR1 -P $MAIN_PID 2>/dev/null
    
    sleep 8
    
    # SCENARIUSZ 2: Wyslij SIGUSR2 (zamkniecie Trasy 2)
    echo ""
    echo "=== SCENARIUSZ 2: SIGUSR2 (zamkniecie Trasy 2) ==="
    echo "[TEST4] Wysylam SIGUSR2 do wszystkich procesow potomnych..."
    pkill -SIGUSR2 -P $MAIN_PID 2>/dev/null
    
else
    sleep 5
    
    # SCENARIUSZ 1: Zamkniecie Trasy 1 w trakcie wycieczki
    echo ""
    echo "=== SCENARIUSZ 1: SIGUSR1 -> Przewodnik 1 (PID $PID_PRZEWODNIK1) ==="
    echo "[TEST4] Wysylam SIGUSR1 (zamkniecie Trasy 1)..."
    kill -SIGUSR1 $PID_PRZEWODNIK1 2>/dev/null
    
    if [ $? -eq 0 ]; then
        echo "PASS: Sygnal wyslany pomyslnie"
    else
        echo "WARNING: Proces mogl juz sie zakonczyc"
    fi
    
    sleep 8
    
    # SCENARIUSZ 2: Zamkniecie Trasy 2 przed wejsciem grupy
    echo ""
    echo "=== SCENARIUSZ 2: SIGUSR2 -> Przewodnik 2 (PID $PID_PRZEWODNIK2) ==="
    echo "[TEST4] Wysylam SIGUSR2 (zamkniecie Trasy 2)..."
    kill -SIGUSR2 $PID_PRZEWODNIK2 2>/dev/null
    
    if [ $? -eq 0 ]; then
        echo "PASS: Sygnal wyslany pomyslnie"
    else
        echo "WARNING: Proces mogl juz sie zakonczyc"
    fi
fi

# Poczekaj na reakcje
echo ""
echo "[TEST4] Czekam 15s na reakcje systemu..."
sleep 15

# Zakoncz proces glowny jesli nadal dziala
if kill -0 $MAIN_PID 2>/dev/null; then
    echo "[TEST4] Koncze proces glowny..."
    kill -TERM $MAIN_PID 2>/dev/null
    sleep 3
    
    if kill -0 $MAIN_PID 2>/dev/null; then
        echo "[TEST4] Wymuszam zakonczenie (SIGKILL)..."
        kill -9 $MAIN_PID 2>/dev/null
    fi
fi

wait $MAIN_PID 2>/dev/null

echo ""
echo "=== ANALIZA REAKCJI NA SYGNAŁY ==="
echo ""

# === SPRAWDŹ CZY BYŁY WYCIECZKI ===
WYCIECZKI=$(grep -c "Wycieczka #.*" test4_output.log)
DOKONCZONO=$(grep -c "Wycieczka.*zakończona SUKCESEM" test4_output.log)

echo "--- Przeprowadzone wycieczki ---"
echo "  - Rozpoczęto: $WYCIECZKI"
echo "  - Dokończono: $DOKONCZONO"

if [ "$WYCIECZKI" -gt 0 ]; then
    echo "PASS: System przeprowadził wycieczki ($WYCIECZKI)"
else
    echo "WARNING: Brak wycieczek"
fi

echo ""

echo "--- Sygnały zamknięcia ---"

ZAMKNIECIE=$(grep -c "Zamknięcie.*PODCZAS\|Zamknięcie.*przed\|Zamknięcie.*PO" test4_output.log)
REAKCJE=$(grep -c "flaga_zamkniecie\|Jaskinia zamknięta" test4_output.log)
ANULACJE=$(grep -c "ANULOWANIE.*grupy\|Grupa anulowana" test4_output.log)

echo "  - Komunikatów zamknięcia: $ZAMKNIECIE"
echo "  - Reakcji procesów: $REAKCJE"
echo "  - Anulowanych grup: $ANULACJE"

if [ "$ZAMKNIECIE" -gt 0 ]; then
    echo "PASS: System wykrył zamknięcie jaskini"
else
    echo "INFO: Symulacja mogła zakończyć się przed zamknięciem"
fi

if [ "$DOKONCZONO" -gt 0 ]; then
    echo "PASS: Wycieczki w trakcie zostały dokończone ($DOKONCZONO)"
fi

if [ "$ANULACJE" -gt 0 ]; then
    echo "PASS: Nowe grupy zostały anulowane ($ANULACJE)"
fi

echo ""

echo "--- Cleanup zasobów IPC ---"

CLEANUP_SHM=$(grep -c "Usunięto pamięć dzieloną\|shmctl.*IPC_RMID" test4_output.log)
CLEANUP_SEM=$(grep -c "Usunięto semafory\|semctl.*IPC_RMID" test4_output.log)
CLEANUP_MSG=$(grep -c "Usunięto kolejkę\|msgctl.*IPC_RMID" test4_output.log)

if [ "$CLEANUP_SHM" -gt 0 ]; then
    echo "  - Pamięć dzielona: OK"
else
    echo "  - Pamięć dzielona: BRAK"
fi

if [ "$CLEANUP_SEM" -gt 0 ]; then
    echo "  - Semafory: OK"
else
    echo "  - Semafory: BRAK"
fi

if [ "$CLEANUP_MSG" -gt 0 ]; then
    echo "  - Kolejka msg: OK"
else
    echo "  - Kolejka msg: BRAK"
fi

# Sprawdź czy nie ma śmieci
SMIECI=$(ipcs -a 2>/dev/null | grep $USER | wc -l)

if [ "$SMIECI" -eq 0 ]; then
    echo "PASS: Brak pozostałości IPC w systemie"
else
    echo "WARNING: Znaleziono $SMIECI pozostałości IPC"
    echo "   Czyszczę..."
    ipcrm -a 2>/dev/null
fi

echo ""

echo "=== PODSUMOWANIE TEST 4 ==="

SCORE=0
[ "$WYCIECZKI" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$ZAMKNIECIE" -gt 0 ] || [ "$REAKCJE" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$DOKONCZONO" -gt 0 ] || [ "$ANULACJE" -gt 0 ] && SCORE=$((SCORE + 1))
[ "$SMIECI" -eq 0 ] && SCORE=$((SCORE + 1))

echo "Wynik: $SCORE/4"

if [ "$SCORE" -ge 3 ]; then
    echo "TEST 4 ZALICZONY"
    exit 0
else
    echo "TEST 4 WYMAGA UWAGI"
    exit 1
fi