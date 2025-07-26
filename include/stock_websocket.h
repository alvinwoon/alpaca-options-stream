#ifndef STOCK_WEBSOCKET_H
#define STOCK_WEBSOCKET_H

#include "types.h"
#include <pthread.h>

#define MAX_UNDERLYINGS 50

// Stock price cache entry
typedef struct {
    char symbol[16];           // Underlying symbol (AAPL, QQQ, etc.)
    double last_price;         // Last trade price
    double bid_price;          // Best bid price
    double ask_price;          // Best ask price
    int last_size;             // Last trade size
    int bid_size;              // Bid size
    int ask_size;              // Ask size
    char last_exchange[8];     // Last trade exchange
    char bid_exchange[8];      // Bid exchange
    char ask_exchange[8];      // Ask exchange
    char timestamp[32];        // Last update timestamp
    int is_valid;              // Data validity flag
    pthread_rwlock_t lock;     // Thread-safe access
} underlying_price_t;

// Stock WebSocket management
typedef struct stock_client_s {
    struct lws *stock_wsi;
    struct lws_context *stock_context;
    int stock_authenticated;
    int stock_subscribed;
    char underlying_symbols[MAX_UNDERLYINGS][16];
    int underlying_count;
    underlying_price_t price_cache[MAX_UNDERLYINGS];
} stock_client_t;

// Stock WebSocket functions
int stock_websocket_connect(alpaca_client_t *client);
int init_stock_client_for_mock(alpaca_client_t *client);
void stock_websocket_disconnect(alpaca_client_t *client);
int stock_websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                            void *user, void *in, size_t len);

// Stock message handling
void send_stock_auth_message(struct lws *wsi, alpaca_client_t *client);
void send_stock_subscription_message(struct lws *wsi, alpaca_client_t *client);
void process_stock_message(const char *data, size_t len, alpaca_client_t *client);

// Price cache management
void init_price_cache(alpaca_client_t *client);
void cleanup_price_cache(alpaca_client_t *client);
double get_underlying_price(alpaca_client_t *client, const char *symbol);
int update_underlying_price(alpaca_client_t *client, const char *symbol, 
                           double price, const char *timestamp);

// Utility functions
void extract_underlying_symbols(alpaca_client_t *client);
const char* extract_underlying_from_option(const char *option_symbol);

#endif // STOCK_WEBSOCKET_H