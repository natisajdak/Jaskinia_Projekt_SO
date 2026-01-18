# 🏔️ Jaskinia  
### Symulacja wieloprocesowego systemu zarządzania ruchem turystycznym

---

## 📌 Cel projektu

Celem projektu jest **implementacja systemu symulującego działanie jaskini turystycznej** z wykorzystaniem mechanizmów **IPC systemu UNIX/Linux**.  
Projekt demonstruje praktyczne zastosowanie programowania współbieżnego oraz komunikacji międzyprocesowej w realistycznym scenariuszu.

System prezentuje w szczególności:
- synchronizację procesów i wątków w środowisku współbieżnym,
- komunikację międzyprocesową (IPC) z użyciem wielu mechanizmów,
- zarządzanie zasobami współdzielonymi z kontrolą dostępu,
- obsługę sygnałów asynchronicznych do koordynacji pracy procesów,
- zapobieganie zakleszczeniom (deadlock prevention).

---

## 🧭 Opis problemu

Jaskinia turystyczna funkcjonuje w określonych godzinach (`Tp` – `Tk`) i oferuje zwiedzanie dwiema trasami:
- **Trasa 1** – pojemność `N1`,
- **Trasa 2** – pojemność `N2`.

Wejście i wyjście z jaskini odbywa się przez **wąską kładkę** o pojemności `K`, gdzie:
- `K < Ni`,
- w danej chwili możliwy jest **ruch tylko w jednym kierunku** (wejście lub wyjście).

Zwiedzający pojawiają się losowo, mają różny wiek i podlegają określonemu regulaminowi. System musi zagwarantować poprawną synchronizację, brak przekroczeń limitów oraz sprawne i bezpieczne działanie symulacji.

---

## 📜 Regulamin zwiedzania

System automatycznie weryfikuje warunki wejścia:

- 👶 **Dzieci < 3 lat**  
  Wstęp bezpłatny.

- 🧒 **Dzieci < 8 lat**  
  Wymagany opiekun (osoba dorosła), możliwe zwiedzanie **wyłącznie Trasą 2**.

- 👴 **Seniorzy > 75 lat**  
  Zwiedzanie **tylko Trasą 2**.

- ⭐ **Zwiedzający VIP (powrót tego samego dnia, ok. 10%)**  
  - 50% zniżki,
  - wejście bez kolejki,
  - obowiązek wyboru **innej trasy** niż poprzednio (zgodnie z regulaminem).

---

## ⚙️ Architektura i technologie

Projekt został zrealizowany w języku **C** z wykorzystaniem standardu **POSIX** oraz mechanizmów **System V IPC**.  
Symulacja ma charakter **rozproszony** – brak centralnego zarządcy logiki, a każdy aktor działa jako niezależny proces.

### 🛠️ Wykorzystane mechanizmy

- **Procesy (`fork`, `exec`, `wait`)**  
  Każda rola systemowa działa jako osobny proces.

- **Wątki (`pthread`)**  
  Obsługa relacji **Opiekun – Dziecko**, zsynchronizowane zwiedzanie w ramach jednej grupy.

- **Pamięć dzielona (`shm`)**  
  Przechowywanie globalnego stanu jaskini (liczniki, flagi, stan tras).

- **Semafory (`sem`)**  
  Synchronizacja dostępu do:
  - kładki,
  - tras,
  - sekcji krytycznych (mutex),
  - sygnalizacji zdarzeń.

- **Kolejki komunikatów (`msg`)**  
  Komunikacja Zwiedzający ↔ Kasjer (kupno biletów, priorytety VIP).

- **Sygnały (`signal`, `sigaction`)**  
  Obsługa zdarzeń asynchronicznych (zamykanie tras, ewakuacja, kończenie pracy).

---

## 🧩 Role systemowe
- **Main** – inicjalizacja IPC, walidacja, sprzątanie zasobów  
- **Generator** – losowe tworzenie zwiedzających  
- **Kasjer** – sprzedaż biletów, zniżki, priorytety  
- **Przewodnicy** – grupy, kładka, limity tras  
- **Strażnik** – czas pracy, sygnały zamknięcia  
- **Zwiedzający** – procesy + wątki (opiekun–dziecko)


## 🔒 Synchronizacja i bezpieczeństwo

- brak przekroczenia pojemności tras i kładki,
- jednokierunkowy ruch na kładce,
- brak zakleszczeń i zagłodzeń procesów,
- poprawne czyszczenie zasobów IPC po zakończeniu symulacji,
- obsługa błędów funkcji systemowych (`perror`, `errno`).

---

## 📋 Testy
- race condition na kładce,
- czystość zasobów IPC,
- ruch jednokierunkowy,
- regulamin biletowy,
- limity `N1/N2`,
- sygnały awaryjne,
- synchronizacja opiekun–dziecko,
- priorytety powtórek.

---

## ✨ Dodatkowe cechy

- rozproszona architektura (brak centralnego sterownika),
- realistyczny model ruchu turystycznego,
- obsługa sygnałów w trakcie działania systemu,
- kolorowe logi ułatwiające analizę przebiegu symulacji,
- mechanizm przyspieszenia czasu symulacji.

---

## 🧪 Raportowanie i logi

Przebieg symulacji zapisywany jest do **plików tekstowych**, zawierających:
- zdarzenia systemowe,
- wejścia i wyjścia zwiedzających,
- decyzje kasjera i przewodników,
- reakcje na sygnały strażnika.

---

## 💻 Środowisko programistyczne

- **System operacyjny:** Ubuntu 22.04.5 LTS (WSL2)  
- **Jądro:** 5.10.16.3-microsoft-standard-WSL2  
- **Kompilator:** gcc 11.4.0  
- **Standard języka:** C99 + POSIX.1-2008  
- **IPC:** System V (shm, sem, msg)  
- **Wielowątkowość:** POSIX Threads (`pthread`)  
- **Budowanie projektu:** GNU Make  

---

## 🚀 Instrukcja Uruchomienia

### Wymagania
- System Linux (testowane na **Ubuntu 22.04 LTS / WSL2**)
- Kompilator **gcc ≥ 11**
- GNU Make
- Obsługa **System V IPC**
- Biblioteka **pthread**


### Kompilacja

W katalogu głównym projektu wykonaj:

```bash
make clean
make