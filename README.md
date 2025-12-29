# Temat 13 - Jaskinia

## Opis projektu
Celem projektu jest symulacja funkcjonowania obiektu turystycznego "Jaskinia" w środowisku wieloprocesowym. System zarządza ruchem turystycznym na dwóch trasach (Trasa 1 i Trasa 2) z uwzględnieniem kładki do wejścia/wyjścia turystów z jaskini.

### Główne założenia i ograniczenia:
* **Zasoby:**
    * Dwie trasy o pojemnościach `N1` i `N2`.
    * Wspólna kładka o pojemności `K` (`K < Ni`).
* **Synchronizacja:** Kładka obsługuje ruch jednokierunkowy. Wymagana jest ścisła synchronizacja, aby uniknąć zakleszczeń i przekroczenia limitu `K` osób na kładce jednocześnie.
* **Regulamin:**
    * Dzieci < 3 lat: wstęp wolny.
    * Dzieci < 8 lat: tylko z opiekunem, wyłącznie Trasa 2.
    * Seniorzy > 75 lat: wyłącznie Trasa 2.
    * Osoby powtarzające zwiedzanie (VIP): zniżka, wejście bez kolejki, wymóg zmiany trasy.
* **Czas pracy:** Jaskinia działa w godzinach od `Tp` do `Tk`.
* **Sytuacje awaryjne:** Obsługa sygnałów od Strażnika (zatrzymanie wpuszczania grup lub przerwanie zwiedzania).
