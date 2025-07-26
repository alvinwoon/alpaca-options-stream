#include "../include/mock_data.h"
#include "../include/message_parser.h"
#include "../include/display.h"
#include "../include/stock_websocket.h"
#include "../include/symbol_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>

// Mock data configuration
static int mock_interval_ms = 2000;  // Default 2000ms (2 seconds) between updates
static double volatility_factor = 0.02;  // 2% volatility
static int mock_running = 0;
static pthread_t mock_thread;
static alpaca_client_t *mock_client = NULL;

// Price tracking for realistic movements
typedef struct {
    char symbol[64];
    double last_trade_price;
    double bid_price;
    double ask_price;
    int trade_size;
    int bid_size;
    int ask_size;
} mock_price_data_t;

static mock_price_data_t price_data[MAX_SYMBOLS];
static int price_data_count = 0;

// Mock underlying price tracking
typedef struct {
    char symbol[16];
    double price;
    double last_update_time;
} mock_underlying_t;

static mock_underlying_t mock_underlyings[10];  // Support up to 10 underlyings
static int mock_underlying_count = 0;

// Helper functions
static double random_double(double min, double max) {
    return min + ((double)rand() / RAND_MAX) * (max - min);
}

static int random_int(int min, int max) {
    return min + rand() % (max - min + 1);
}

static void get_current_timestamp(char *timestamp, size_t size) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    struct tm *tm_info = gmtime(&ts.tv_sec);
    snprintf(timestamp, size, "%04d-%02d-%02dT%02d:%02d:%02d.%09ldZ",
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, ts.tv_nsec);
}

static double get_realistic_underlying_price(const char *symbol) {
    // Return realistic prices for common underlyings
    if (strstr(symbol, "AAPL") != NULL) return 150.0 + random_double(-5.0, 5.0);
    if (strstr(symbol, "QQQ") != NULL) return 350.0 + random_double(-10.0, 10.0);
    if (strstr(symbol, "SPY") != NULL) return 450.0 + random_double(-15.0, 15.0);
    if (strstr(symbol, "TSLA") != NULL) return 200.0 + random_double(-20.0, 20.0);
    if (strstr(symbol, "MSFT") != NULL) return 300.0 + random_double(-10.0, 10.0);
    if (strstr(symbol, "NVDA") != NULL) return 800.0 + random_double(-40.0, 40.0);
    return 100.0 + random_double(-10.0, 10.0);  // Default
}

static mock_underlying_t* get_or_create_mock_underlying(const char *symbol) {
    // Find existing
    for (int i = 0; i < mock_underlying_count; i++) {
        if (strcmp(mock_underlyings[i].symbol, symbol) == 0) {
            return &mock_underlyings[i];
        }
    }
    
    // Create new if space available
    if (mock_underlying_count < 10) {
        mock_underlying_t *underlying = &mock_underlyings[mock_underlying_count];
        strncpy(underlying->symbol, symbol, sizeof(underlying->symbol) - 1);
        underlying->symbol[sizeof(underlying->symbol) - 1] = '\0';
        underlying->price = get_realistic_underlying_price(symbol);
        underlying->last_update_time = 0;
        mock_underlying_count++;
        return underlying;
    }
    
    return NULL;
}

static void update_mock_underlying_price(alpaca_client_t *client, const char *symbol) {
    mock_underlying_t *underlying = get_or_create_mock_underlying(symbol);
    if (!underlying) return;
    
    // Update price with realistic movement (smaller volatility for underlying)
    double price_change = random_double(-1.0, 1.0) * 0.01 * underlying->price;  // 1% volatility
    underlying->price += price_change;
    
    // Ensure price stays reasonable
    if (underlying->price < 1.0) underlying->price = 1.0;
    
    // Update the stock price cache so get_underlying_price() works
    char timestamp[64];
    get_current_timestamp(timestamp, sizeof(timestamp));
    update_underlying_price(client, symbol, underlying->price, timestamp);
    
    underlying->last_update_time = time(NULL);
}

static mock_price_data_t* get_or_create_price_data(const char *symbol) {
    // Find existing
    for (int i = 0; i < price_data_count; i++) {
        if (strcmp(price_data[i].symbol, symbol) == 0) {
            return &price_data[i];
        }
    }
    
    // Create new if space available
    if (price_data_count < MAX_SYMBOLS) {
        mock_price_data_t *new_data = &price_data[price_data_count];
        strncpy(new_data->symbol, symbol, sizeof(new_data->symbol) - 1);
        
        // Initialize with realistic option prices based on symbol
        if (strstr(symbol, "QQQ") != NULL) {
            new_data->last_trade_price = random_double(1.0, 15.0);
        } else if (strstr(symbol, "AAPL") != NULL) {
            new_data->last_trade_price = random_double(2.0, 25.0);
        } else if (strstr(symbol, "SPY") != NULL) {
            new_data->last_trade_price = random_double(0.5, 20.0);
        } else {
            new_data->last_trade_price = random_double(0.5, 10.0);
        }
        
        // Set bid/ask around the trade price
        double spread = new_data->last_trade_price * 0.02; // 2% spread
        new_data->bid_price = new_data->last_trade_price - spread/2;
        new_data->ask_price = new_data->last_trade_price + spread/2;
        
        new_data->trade_size = random_int(1, 50);
        new_data->bid_size = random_int(1, 100);
        new_data->ask_size = random_int(1, 100);
        
        price_data_count++;
        return new_data;
    }
    
    return NULL;
}

void generate_mock_trade(alpaca_client_t *client, const char *symbol) {
    mock_price_data_t *price_data = get_or_create_price_data(symbol);
    if (!price_data) return;
    
    // Generate realistic price movement
    double price_change = (random_double(-1.0, 1.0) * volatility_factor * price_data->last_trade_price);
    price_data->last_trade_price += price_change;
    
    // Ensure price doesn't go negative
    if (price_data->last_trade_price < 0.01) {
        price_data->last_trade_price = 0.01;
    }
    
    // Random trade size
    price_data->trade_size = random_int(1, 100);
    
    // Update option data
    pthread_mutex_lock(&client->data_mutex);
    option_data_t *data = find_or_create_option_data(symbol, client);
    if (data) {
        data->last_price = price_data->last_trade_price;
        data->last_size = price_data->trade_size;
        
        // Mock exchange codes
        const char *exchanges[] = {"N", "C", "A", "P", "B"};
        strcpy(data->trade_exchange, exchanges[rand() % 5]);
        
        // Mock trade conditions
        const char *conditions[] = {"S", "R", "T", "U", "V"};
        strcpy(data->trade_condition, conditions[rand() % 5]);
        
        get_current_timestamp(data->trade_time, sizeof(data->trade_time));
        data->has_trade = 1;
        
        // Calculate Black-Scholes analytics
        calculate_option_analytics(data, client);
    }
    pthread_mutex_unlock(&client->data_mutex);
}

void generate_mock_quote(alpaca_client_t *client, const char *symbol) {
    mock_price_data_t *price_data = get_or_create_price_data(symbol);
    if (!price_data) return;
    
    // Generate realistic bid/ask around current price
    double mid_price = price_data->last_trade_price;
    double spread_pct = random_double(0.01, 0.05); // 1-5% spread
    double spread = mid_price * spread_pct;
    
    price_data->bid_price = mid_price - spread/2 + random_double(-spread*0.2, spread*0.2);
    price_data->ask_price = mid_price + spread/2 + random_double(-spread*0.2, spread*0.2);
    
    // Ensure bid < ask and both positive
    if (price_data->bid_price < 0.01) price_data->bid_price = 0.01;
    if (price_data->ask_price <= price_data->bid_price) {
        price_data->ask_price = price_data->bid_price + 0.05;
    }
    
    // Random sizes
    price_data->bid_size = random_int(1, 150);
    price_data->ask_size = random_int(1, 150);
    
    // Update option data
    pthread_mutex_lock(&client->data_mutex);
    option_data_t *data = find_or_create_option_data(symbol, client);
    if (data) {
        data->bid_price = price_data->bid_price;
        data->bid_size = price_data->bid_size;
        data->ask_price = price_data->ask_price;
        data->ask_size = price_data->ask_size;
        
        // Mock exchange codes
        const char *exchanges[] = {"N", "C", "A", "P", "B"};
        strcpy(data->bid_exchange, exchanges[rand() % 5]);
        strcpy(data->ask_exchange, exchanges[rand() % 5]);
        
        // Mock quote conditions
        const char *conditions[] = {"A", "B", "R", "U", "Y"};
        strcpy(data->quote_condition, conditions[rand() % 5]);
        
        get_current_timestamp(data->quote_time, sizeof(data->quote_time));
        data->has_quote = 1;
        
        // Calculate Black-Scholes analytics
        calculate_option_analytics(data, client);
    }
    pthread_mutex_unlock(&client->data_mutex);
}

static void* mock_data_thread(void *arg) {
    (void)arg; // Suppress unused parameter warning
    
    srand(time(NULL)); // Initialize random seed
    
    printf("Starting mock data stream (interval: %dms, volatility: %.1f%%)\n", 
           mock_interval_ms, volatility_factor * 100);
    
    while (mock_running && mock_client) {
        // First, update underlying prices for all unique underlyings
        for (int i = 0; i < mock_client->symbol_count; i++) {
            const char *underlying = extract_underlying_from_option(mock_client->symbols[i]);
            if (underlying) {
                update_mock_underlying_price(mock_client, underlying);
            }
        }
        
        // Then generate option data for each symbol
        for (int i = 0; i < mock_client->symbol_count; i++) {
            // Randomly choose between trade or quote (or both)
            int data_type = rand() % 3;
            
            switch (data_type) {
                case 0: // Trade only
                    generate_mock_trade(mock_client, mock_client->symbols[i]);
                    break;
                case 1: // Quote only
                    generate_mock_quote(mock_client, mock_client->symbols[i]);
                    break;
                case 2: // Both trade and quote
                    if (rand() % 2) {
                        generate_mock_trade(mock_client, mock_client->symbols[i]);
                        usleep(50000); // Small delay between trade and quote
                    }
                    generate_mock_quote(mock_client, mock_client->symbols[i]);
                    break;
            }
            
            // Small delay between symbols to prevent overwhelming the system
            usleep(50000); // 50ms
        }
        
        // Note: Display thread handles rendering independently
        
        // Wait for next interval
        usleep(mock_interval_ms * 1000);
    }
    
    printf("Mock data stream stopped\n");
    return NULL;
}

void start_mock_data_stream(alpaca_client_t *client) {
    if (mock_running) {
        printf("Mock data stream already running\n");
        return;
    }
    
    mock_client = client;
    mock_running = 1;
    
    // Initialize price data for all symbols
    price_data_count = 0;
    for (int i = 0; i < client->symbol_count; i++) {
        get_or_create_price_data(client->symbols[i]);
        
        // Also initialize underlying prices immediately
        const char *underlying = extract_underlying_from_option(client->symbols[i]);
        if (underlying) {
            update_mock_underlying_price(client, underlying);
        }
    }
    
    // Start the mock data thread
    if (pthread_create(&mock_thread, NULL, mock_data_thread, NULL) != 0) {
        printf("Failed to create mock data thread\n");
        mock_running = 0;
        return;
    }
    
    printf("Mock data stream started for %d symbols\n", client->symbol_count);
}

void stop_mock_data_stream(void) {
    if (!mock_running) return;
    
    mock_running = 0;
    
    // Wait for thread to finish
    pthread_join(mock_thread, NULL);
    
    mock_client = NULL;
    price_data_count = 0;
}

void set_mock_data_interval(int milliseconds) {
    if (milliseconds < 100) milliseconds = 100; // Minimum 100ms
    mock_interval_ms = milliseconds;
}

void set_mock_data_volatility(double volatility) {
    if (volatility < 0.001) volatility = 0.001; // Minimum 0.1%
    if (volatility > 0.1) volatility = 0.1;     // Maximum 10%
    volatility_factor = volatility;
}