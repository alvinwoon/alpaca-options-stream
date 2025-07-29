#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"

// Volatility dislocation analysis
typedef struct {
    int vanna_anomaly;      // 1 if Vanna sign/magnitude anomalous
    int volga_anomaly;      // 1 if Volga >2x normal or near zero
    int charm_anomaly;      // 1 if Charm wrong sign or excessive
    double vanna_volga_ratio; // Vanna/Volga ratio (normal: 0.1-0.3)
    char alert_message[256]; // Human-readable alert
    char trade_recommendation[512]; // Specific trade recommendation
    
    // RV analysis integration
    int iv_rv_anomaly;      // 1 if IV vs RV spread is extreme
    double iv_rv_spread;    // IV - RV spread
    char rv_signal[32];     // "EXPENSIVE", "CHEAP", "NEUTRAL"
} dislocation_alert_t;

// Display functions
void display_option_data(alpaca_client_t *client);
void display_symbols_list(alpaca_client_t *client, const char *title);

// Dislocation analysis functions  
dislocation_alert_t analyze_volatility_dislocation(option_data_t *data, alpaca_client_t *client);
void generate_trade_recommendation(option_data_t *data, dislocation_alert_t *alert);
void display_dislocation_alerts(alpaca_client_t *client);

// Display threading functions
int start_display_thread(alpaca_client_t *client);
void stop_display_thread(alpaca_client_t *client);

#endif // DISPLAY_H