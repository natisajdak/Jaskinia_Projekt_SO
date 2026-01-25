#!/bin/bash
for i in {1..5}; do
  echo "ITERACJA $i/5"
  
  timeout 8 ./bin/main &
  PID=$!
  sleep 5
  
  echo "→ Zatrzymuję program..."
  kill -SIGINT $PID 2>/dev/null
  wait $PID 2>/dev/null
  
  sleep 2
  
  echo "→ Sprawdzam zasoby IPC..."
  ZASOBY=$(ipcs -a | grep $(whoami) | wc -l)
  
  if [ $ZASOBY -eq 0 ]; then
    echo "Iteracja $i: CZYSTE"
  else
    echo "Iteracja $i: WYCIEK ($ZASOBY zasobów)!"
    ipcs -a | grep $(whoami)
  fi
  
  echo ""
  sleep 1
done


echo "TEST ZAKOŃCZONY"

FINALNE=$(ipcs -a | grep $(whoami) | wc -l)
if [ $FINALNE -eq 0 ]; then
  echo "SUKCES: Brak wycieków!"
else
  echo "BŁĄD: Pozostało $FINALNE zasobów!"
fi
