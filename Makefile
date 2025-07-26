CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -Iinclude
LIBS = -lwebsockets -lmsgpackc -lssl -lcrypto -lcurl -lcjson -lpthread -lm

# Detect OS for library paths
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - using Homebrew paths
    CFLAGS += -I/opt/homebrew/include -I/usr/local/include
    LDFLAGS += -L/opt/homebrew/lib -L/usr/local/lib
endif

# Project structure
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Source files
MAIN_SOURCES = $(SRCDIR)/main.c $(SRCDIR)/websocket.c $(SRCDIR)/stock_websocket.c \
               $(SRCDIR)/api_client.c $(SRCDIR)/symbol_parser.c $(SRCDIR)/display.c \
               $(SRCDIR)/message_parser.c $(SRCDIR)/mock_data.c $(SRCDIR)/fred_api.c \
               $(SRCDIR)/black_scholes.c $(SRCDIR)/volatility_smile.c $(SRCDIR)/config.c

SYMBOL_SOURCES = get_option_symbols.c

# Object files
MAIN_OBJECTS = $(MAIN_SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
TARGET = alpaca_options_stream
SYMBOL_TOOL = get_option_symbols

.PHONY: all clean install-deps setup

all: setup $(TARGET) $(SYMBOL_TOOL)

setup:
	@mkdir -p $(OBJDIR)

$(TARGET): $(MAIN_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(SYMBOL_TOOL): $(SYMBOL_SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lcurl -lcjson

clean:
	rm -rf $(OBJDIR) $(TARGET) $(SYMBOL_TOOL)

install-deps:
	@echo "Installing dependencies..."
	@if command -v brew >/dev/null 2>&1; then \
		echo "Using Homebrew to install dependencies..."; \
		brew install libwebsockets msgpack openssl curl; \
	elif command -v apt-get >/dev/null 2>&1; then \
		echo "Using apt-get to install dependencies..."; \
		sudo apt-get update; \
		sudo apt-get install libwebsockets-dev libmsgpack-dev libssl-dev libcurl4-openssl-dev libcjson-dev; \
	elif command -v yum >/dev/null 2>&1; then \
		echo "Using yum to install dependencies..."; \
		sudo yum install libwebsockets-devel msgpack-devel openssl-devel libcurl-devel cjson-devel; \
	else \
		echo "Please install libwebsockets, msgpack, openssl, curl, and cjson manually"; \
	fi

run: $(TARGET)
	@echo "Usage: ./$(TARGET) [OPTIONS] [ARGS...]"
	@echo ""
	@echo "Setup (first time):"
	@echo "  cp config.example.json config.json"
	@echo "  # Edit config.json with your API keys"
	@echo ""
	@echo "Modes:"
	@echo "1. Direct symbols: ./$(TARGET) SYMBOL1 SYMBOL2 ..."
	@echo "   Example: ./$(TARGET) AAPL251220C00150000"
	@echo ""
	@echo "2. Auto-fetch (dates only): ./$(TARGET) UNDERLYING EXP_DATE_GTE EXP_DATE_LTE"
	@echo "   Example: ./$(TARGET) AAPL 2025-08-01 2025-09-01"
	@echo ""
	@echo "3. Auto-fetch (dates + strikes): ./$(TARGET) UNDERLYING EXP_DATE_GTE EXP_DATE_LTE STRIKE_GTE STRIKE_LTE"
	@echo "   Example: ./$(TARGET) AAPL 2025-08-01 2025-09-01 150.00 160.00"
	@echo ""
	@echo "4. Mock mode (development): ./$(TARGET) --mock SYMBOL1 SYMBOL2 ..."
	@echo "   Example: ./$(TARGET) --mock AAPL251220C00150000 AAPL251220P00150000"
	@echo ""
	@echo "Options:"
	@echo "  --setup    Show API configuration help"
	@echo "  --help     Show usage help"

symbols: $(SYMBOL_TOOL)
	@echo "Usage: ./$(SYMBOL_TOOL) <API_KEY> <API_SECRET> <SYMBOL> <EXPIRATION_DATE_GTE> <EXPIRATION_DATE_LTE> [STRIKE_GTE] [STRIKE_LTE]"
	@echo "Examples:"
	@echo "  Dates only:     ./$(SYMBOL_TOOL) YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20"
	@echo "  With strikes:   ./$(SYMBOL_TOOL) YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20 150.00 160.00"

help:
	@echo "Available targets:"
	@echo "  all         - Build both programs"
	@echo "  clean       - Remove built files and object directory"
	@echo "  install-deps- Install required dependencies"
	@echo "  run         - Show streaming usage instructions"
	@echo "  symbols     - Show symbol lookup usage instructions"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Project Structure:"
	@echo "  src/        - Source files"
	@echo "  include/    - Header files"
	@echo "  obj/        - Object files (created during build)"

# Dependencies
$(OBJDIR)/main.o: $(INCDIR)/types.h $(INCDIR)/websocket.h $(INCDIR)/api_client.h $(INCDIR)/display.h $(INCDIR)/mock_data.h $(INCDIR)/fred_api.h
$(OBJDIR)/websocket.o: $(INCDIR)/websocket.h $(INCDIR)/stock_websocket.h $(INCDIR)/types.h $(INCDIR)/message_parser.h $(INCDIR)/display.h
$(OBJDIR)/stock_websocket.o: $(INCDIR)/stock_websocket.h $(INCDIR)/types.h
$(OBJDIR)/api_client.o: $(INCDIR)/api_client.h $(INCDIR)/types.h $(INCDIR)/display.h
$(OBJDIR)/symbol_parser.o: $(INCDIR)/symbol_parser.h
$(OBJDIR)/display.o: $(INCDIR)/display.h $(INCDIR)/types.h $(INCDIR)/symbol_parser.h $(INCDIR)/volatility_smile.h
$(OBJDIR)/message_parser.o: $(INCDIR)/message_parser.h $(INCDIR)/types.h $(INCDIR)/display.h $(INCDIR)/websocket.h $(INCDIR)/black_scholes.h $(INCDIR)/symbol_parser.h $(INCDIR)/stock_websocket.h
$(OBJDIR)/mock_data.o: $(INCDIR)/mock_data.h $(INCDIR)/types.h $(INCDIR)/message_parser.h $(INCDIR)/display.h
$(OBJDIR)/fred_api.o: $(INCDIR)/fred_api.h $(INCDIR)/types.h $(INCDIR)/api_client.h
$(OBJDIR)/black_scholes.o: $(INCDIR)/black_scholes.h
$(OBJDIR)/volatility_smile.o: $(INCDIR)/volatility_smile.h $(INCDIR)/types.h $(INCDIR)/symbol_parser.h