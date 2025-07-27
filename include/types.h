#ifndef TYPES_H
#define TYPES_H

#include <libwebsockets.h>
#include <time.h>
#include <pthread.h>
#include "black_scholes.h"

#define MAX_PAYLOAD 4096
#define MAX_SYMBOLS 100

typedef struct {
    char symbol[64];
    // Quote data
    double bid_price;
    int bid_size;
    char bid_exchange[8];
    double ask_price;
    int ask_size;
    char ask_exchange[8];
    char quote_time[32];
    char quote_condition[8];
    int has_quote;
    // Trade data
    double last_price;
    int last_size;
    char trade_exchange[8];
    char trade_time[32];
    char trade_condition[8];
    int has_trade;
    // Black-Scholes analytics
    bs_result_t bs_analytics;
    double underlying_price;
    double strike;
    double time_to_expiry;
    int is_call;
    int analytics_valid;  // 1 if BS analytics are valid, 0 otherwise
    // Previous values for change tracking (only for colored fields)
    double prev_spread;
    double prev_implied_vol;
    double prev_delta;
    double prev_gamma;
    double prev_theta;
    double prev_vega;
    // Previous values for 2nd and 3rd order Greeks
    double prev_vanna;
    double prev_charm;
    double prev_volga;
    double prev_speed;
    double prev_zomma;
    double prev_color;
} option_data_t;

typedef struct {
    char *data;
    size_t size;
} api_response_t;

// Forward declarations to avoid circular dependencies
struct stock_client_s;
struct smile_analysis_s;

typedef struct {
    char *api_key;
    char *api_secret;
    
    // Options WebSocket
    struct lws_context *context;
    struct lws *wsi;
    int authenticated;
    int subscribed;
    
    // Stock WebSocket (forward declaration)
    struct stock_client_s *stock_client;
    
    // Market data
    double risk_free_rate;     // Current risk-free rate (as decimal, e.g., 0.05 = 5%)
    
    // Common
    int interrupted;
    char symbols[MAX_SYMBOLS][32];
    int symbol_count;
    option_data_t option_data[MAX_SYMBOLS];
    int data_count;
    
    // Display threading
    pthread_t display_thread;
    pthread_mutex_t data_mutex;
    int display_running;
    int display_interval_seconds;
    
    // Volatility smile analysis
    struct smile_analysis_s *smile_analysis;
} alpaca_client_t;

#endif // TYPES_H