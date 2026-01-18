# Makefile dla projektu Jaskinia

# Kompilator i flagi
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c99 -D_POSIX_C_SOURCE=200809L -g
LDFLAGS = -pthread

# Katalogi
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
OBJ_DIR = obj
LOG_DIR = logs

# Pliki źródłowe
SOURCES = main.c kasjer.c przewodnik.c zwiedzajacy.c straznik.c generator.c ipc.c utils.c
OBJECTS = $(SOURCES:%.c=$(OBJ_DIR)/%.o)

# Pliki wykonywalne
TARGETS = $(BIN_DIR)/main $(BIN_DIR)/kasjer $(BIN_DIR)/przewodnik \
          $(BIN_DIR)/zwiedzajacy $(BIN_DIR)/straznik $(BIN_DIR)/generator

# Domyślny cel
all: dirs $(TARGETS)

# Tworzenie katalogów
dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR) $(LOG_DIR)

# Kompilacja plików obiektowych
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Linkowanie - main
$(BIN_DIR)/main: $(OBJ_DIR)/main.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Linkowanie - kasjer
$(BIN_DIR)/kasjer: $(OBJ_DIR)/kasjer.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Linkowanie - przewodnik
$(BIN_DIR)/przewodnik: $(OBJ_DIR)/przewodnik.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Linkowanie - zwiedzajacy
$(BIN_DIR)/zwiedzajacy: $(OBJ_DIR)/zwiedzajacy.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Linkowanie - straznik
$(BIN_DIR)/straznik: $(OBJ_DIR)/straznik.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/generator: $(OBJ_DIR)/generator.o $(OBJ_DIR)/ipc.o $(OBJ_DIR)/utils.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Uruchomienie programu
run: all
	@echo "=== URUCHAMIANIE SYMULACJI ==="
	@./$(BIN_DIR)/main

# Czyszczenie plików obiektowych
clean:
	rm -rf $(OBJ_DIR)/*.o

# Czyszczenie wszystkiego
cleanall: clean
	rm -rf $(BIN_DIR)/* $(LOG_DIR)/*
	@echo "Wyczyszczono wszystkie pliki"

# Czyszczenie tylko logów
cleanlogs:
	rm -rf $(LOG_DIR)/*
	@echo "Wyczyszczono logi"

clean_ipc:
	ipcs -s | grep $(USER) | awk '{print $$2}' | xargs -n1 ipcrm -s 2>/dev/null || true
	ipcs -m | grep $(USER) | awk '{print $$2}' | xargs -n1 ipcrm -m 2>/dev/null || true
	ipcs -q | grep $(USER) | awk '{print $$2}' | xargs -n1 ipcrm -q 2>/dev/null || true

# Wyświetlenie logów
logs:
	@echo "=== BILETY ==="
	@cat $(LOG_DIR)/bilety.txt 2>/dev/null || echo "Brak pliku"
	@echo ""
	@echo "=== TRASA 1 ==="
	@cat $(LOG_DIR)/trasa1.txt 2>/dev/null || echo "Brak pliku"
	@echo ""
	@echo "=== TRASA 2 ==="
	@cat $(LOG_DIR)/trasa2.txt 2>/dev/null || echo "Brak pliku"
	@echo ""
	@echo "=== SYMULACJA ==="
	@cat $(LOG_DIR)/symulacja.log 2>/dev/null || echo "Brak pliku"

# Pomoc
help:
	@echo "Dostępne cele:"
	@echo "  make          - kompilacja wszystkich plików"
	@echo "  make run      - kompilacja i uruchomienie"
	@echo "  make clean    - usunięcie plików .o"
	@echo "  make cleanall - usunięcie wszystkich wygenerowanych plików"
	@echo "  make cleanlogs - usunięcie logów"
	@echo "  make clean_ipc - usunięcie zasobów IPC"
	@echo "  make logs     - wyświetlenie logów"
	@echo "  make help     - ten komunikat"

.PHONY: all dirs run clean cleanall cleanlogs logs help