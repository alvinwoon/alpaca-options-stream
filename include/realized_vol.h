#ifndef REALIZED_VOL_H
#define REALIZED_VOL_H

#include <time.h>

#define MAX_PRICE_HISTORY 252  // 1 year of daily data
#define RV_WINDOWS 3          // 10d, 20d, 30d windows

typedef struct {
    double open;
    double high;
    double low;
    double close;
    time_t timestamp;
    int valid;
} ohlc_data_t;

typedef struct {
    char symbol[16];           // Underlying symbol (e.g., "QQQ")
    ohlc_data_t history[MAX_PRICE_HISTORY];
    int data_count;
    int current_index;         // Circular buffer index
    
    // Calculated RV values
    double rv_10d;            // 10-day realized vol
    double rv_20d;            // 20-day realized vol  
    double rv_30d;            // 30-day realized vol
    double rv_trend;          // RV trend (positive = increasing)
    
    // RV statistics
    double rv_mean;           // Historical RV mean
    double rv_std;            // Historical RV standard deviation
    
    time_t last_update;
} realized_vol_t;

typedef struct rv_manager_s {
    realized_vol_t *underlying_rvs;  // Array of RV data per underlying
    int rv_count;
    int initialized;
} rv_manager_t;

// Core RV functions
double calculate_parkinson_rv(ohlc_data_t *data, int periods);
double calculate_garman_klass_rv(ohlc_data_t *data, int periods);
double calculate_close_to_close_rv(ohlc_data_t *data, int periods);

// RV management
rv_manager_t* init_rv_manager(void);
void cleanup_rv_manager(rv_manager_t *manager);
realized_vol_t* get_underlying_rv(rv_manager_t *manager, const char *symbol);
int update_price_data(realized_vol_t *rv, double open, double high, double low, double close);
void calculate_all_rv_metrics(realized_vol_t *rv);

// RV vs IV analysis
typedef struct {
    double iv_rv_spread;      // IV - RV (positive = expensive vol)
    double iv_percentile;     // IV percentile vs historical RV
    int vol_regime;           // 0=low, 1=normal, 2=high vol environment
    char signal[64];          // "EXPENSIVE", "CHEAP", "NEUTRAL"
    char recommendation[128]; // Trading recommendation
} iv_rv_analysis_t;

iv_rv_analysis_t analyze_iv_vs_rv(double implied_vol, realized_vol_t *rv, double days_to_expiry);

#endif // REALIZED_VOL_H