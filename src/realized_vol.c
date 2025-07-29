#include "../include/realized_vol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Calculate Parkinson RV (uses high-low range, more efficient than close-to-close)
double calculate_parkinson_rv(ohlc_data_t *data, int periods) {
    if (!data || periods <= 1) return 0.0;
    
    double sum_log_hl = 0.0;
    int valid_periods = 0;
    
    for (int i = 0; i < periods; i++) {
        if (!data[i].valid || data[i].high <= 0 || data[i].low <= 0) continue;
        if (data[i].high < data[i].low) continue;  // Sanity check
        
        double log_hl = log(data[i].high / data[i].low);
        sum_log_hl += log_hl * log_hl;
        valid_periods++;
    }
    
    if (valid_periods < 5) return 0.0;  // Need minimum data points
    
    // Parkinson estimator: sqrt(sum(ln(H/L)^2) / (4*ln(2)*n)) * sqrt(252)
    double parkinson_var = sum_log_hl / (4.0 * log(2.0) * valid_periods);
    return sqrt(parkinson_var * 252.0);  // Annualized
}

// Calculate Garman-Klass RV (incorporates overnight gaps)
double calculate_garman_klass_rv(ohlc_data_t *data, int periods) {
    if (!data || periods <= 1) return 0.0;
    
    double sum_gk = 0.0;
    int valid_periods = 0;
    
    for (int i = 1; i < periods; i++) {  // Start from 1 to have previous close
        if (!data[i].valid || !data[i-1].valid) continue;
        if (data[i].high <= 0 || data[i].low <= 0 || data[i].close <= 0 || data[i].open <= 0) continue;
        if (data[i-1].close <= 0) continue;
        
        // Overnight return component
        double log_o_c_prev = log(data[i].open / data[i-1].close);
        
        // Intraday component  
        double log_h_c = log(data[i].high / data[i].close);
        double log_h_o = log(data[i].high / data[i].open);
        double log_l_c = log(data[i].low / data[i].close);
        double log_l_o = log(data[i].low / data[i].open);
        
        // Garman-Klass estimator
        double gk_component = log_o_c_prev * log_o_c_prev + 
                             0.5 * (log_h_o * log_h_o + log_l_o * log_l_o) -
                             (2.0 * log(2.0) - 1.0) * (log_h_c * log_h_c + log_l_c * log_l_c);
        
        sum_gk += gk_component;
        valid_periods++;
    }
    
    if (valid_periods < 5) return 0.0;
    
    return sqrt((sum_gk / valid_periods) * 252.0);  // Annualized
}

// Calculate simple close-to-close RV (traditional method)
double calculate_close_to_close_rv(ohlc_data_t *data, int periods) {
    if (!data || periods <= 1) return 0.0;
    
    double sum_log_returns = 0.0;
    int valid_periods = 0;
    
    for (int i = 1; i < periods; i++) {
        if (!data[i].valid || !data[i-1].valid) continue;
        if (data[i].close <= 0 || data[i-1].close <= 0) continue;
        
        double log_return = log(data[i].close / data[i-1].close);
        sum_log_returns += log_return * log_return;
        valid_periods++;
    }
    
    if (valid_periods < 5) return 0.0;
    
    return sqrt((sum_log_returns / valid_periods) * 252.0);  // Annualized
}

// Initialize RV manager
rv_manager_t* init_rv_manager(void) {
    rv_manager_t *manager = malloc(sizeof(rv_manager_t));
    if (!manager) return NULL;
    
    manager->underlying_rvs = NULL;
    manager->rv_count = 0;
    manager->initialized = 1;
    
    return manager;
}

// Cleanup RV manager
void cleanup_rv_manager(rv_manager_t *manager) {
    if (!manager) return;
    
    if (manager->underlying_rvs) {
        free(manager->underlying_rvs);
    }
    free(manager);
}

// Get or create RV data for underlying symbol
realized_vol_t* get_underlying_rv(rv_manager_t *manager, const char *symbol) {
    if (!manager || !symbol) return NULL;
    
    // Look for existing RV data
    for (int i = 0; i < manager->rv_count; i++) {
        if (strcmp(manager->underlying_rvs[i].symbol, symbol) == 0) {
            return &manager->underlying_rvs[i];
        }
    }
    
    // Create new RV entry
    manager->underlying_rvs = realloc(manager->underlying_rvs, 
                                    (manager->rv_count + 1) * sizeof(realized_vol_t));
    if (!manager->underlying_rvs) return NULL;
    
    realized_vol_t *new_rv = &manager->underlying_rvs[manager->rv_count];
    memset(new_rv, 0, sizeof(realized_vol_t));
    
    strncpy(new_rv->symbol, symbol, sizeof(new_rv->symbol) - 1);
    new_rv->current_index = 0;
    new_rv->data_count = 0;
    new_rv->last_update = 0;
    
    manager->rv_count++;
    return new_rv;
}

// Update price data (circular buffer)
int update_price_data(realized_vol_t *rv, double open, double high, double low, double close) {
    if (!rv || open <= 0 || high <= 0 || low <= 0 || close <= 0) return 0;
    if (high < low || high < open || high < close || low > open || low > close) return 0;
    
    ohlc_data_t *current = &rv->history[rv->current_index];
    current->open = open;
    current->high = high;
    current->low = low;
    current->close = close;
    current->timestamp = time(NULL);
    current->valid = 1;
    
    rv->current_index = (rv->current_index + 1) % MAX_PRICE_HISTORY;
    if (rv->data_count < MAX_PRICE_HISTORY) {
        rv->data_count++;
    }
    
    rv->last_update = time(NULL);
    
    // Recalculate RV metrics
    calculate_all_rv_metrics(rv);
    
    return 1;
}

// Calculate all RV metrics for the underlying
void calculate_all_rv_metrics(realized_vol_t *rv) {
    if (!rv || rv->data_count < 10) return;
    
    // Create ordered array (most recent first)
    ohlc_data_t ordered_data[MAX_PRICE_HISTORY];
    int ordered_count = 0;
    
    // Copy data in reverse chronological order
    for (int i = 0; i < rv->data_count; i++) {
        int idx = (rv->current_index - 1 - i + MAX_PRICE_HISTORY) % MAX_PRICE_HISTORY;
        if (rv->history[idx].valid) {
            ordered_data[ordered_count] = rv->history[idx];
            ordered_count++;
        }
    }
    
    // Calculate RV for different windows
    if (ordered_count >= 10) {
        rv->rv_10d = calculate_parkinson_rv(ordered_data, 10);
    }
    if (ordered_count >= 20) {
        rv->rv_20d = calculate_parkinson_rv(ordered_data, 20);
    }
    if (ordered_count >= 30) {
        rv->rv_30d = calculate_parkinson_rv(ordered_data, 30);
    }
    
    // Calculate RV trend (10d vs 20d)
    if (rv->rv_10d > 0 && rv->rv_20d > 0) {
        rv->rv_trend = (rv->rv_10d - rv->rv_20d) / rv->rv_20d;
    }
    
    // Calculate historical statistics
    if (ordered_count >= 60) {
        double rv_values[60];
        int rv_count = 0;
        
        // Calculate rolling 20-day RV for last 60 periods
        for (int i = 0; i < 40 && i + 20 < ordered_count; i++) {
            double rv = calculate_parkinson_rv(&ordered_data[i], 20);
            if (rv > 0) {
                rv_values[rv_count++] = rv;
            }
        }
        
        if (rv_count > 10) {
            // Calculate mean
            double sum = 0;
            for (int i = 0; i < rv_count; i++) {
                sum += rv_values[i];
            }
            rv->rv_mean = sum / rv_count;
            
            // Calculate standard deviation
            double sum_sq = 0;
            for (int i = 0; i < rv_count; i++) {
                double diff = rv_values[i] - rv->rv_mean;
                sum_sq += diff * diff;
            }
            rv->rv_std = sqrt(sum_sq / rv_count);
        }
    }
}

// Analyze IV vs RV for trading signals
iv_rv_analysis_t analyze_iv_vs_rv(double implied_vol, realized_vol_t *rv, double days_to_expiry) {
    iv_rv_analysis_t analysis = {0};
    
    if (!rv || implied_vol <= 0 || rv->rv_20d <= 0) {
        strcpy(analysis.signal, "NO_DATA");
        strcpy(analysis.recommendation, "Insufficient RV data");
        return analysis;
    }
    
    // Use appropriate RV based on time to expiry
    double relevant_rv = rv->rv_20d;  // Default to 20-day
    if (days_to_expiry < 15 && rv->rv_10d > 0) {
        relevant_rv = rv->rv_10d;      // Use 10-day for short-term
    } else if (days_to_expiry > 45 && rv->rv_30d > 0) {
        relevant_rv = rv->rv_30d;      // Use 30-day for longer-term
    }
    
    analysis.iv_rv_spread = implied_vol - relevant_rv;
    
    // Calculate IV percentile vs historical RV
    if (rv->rv_mean > 0 && rv->rv_std > 0) {
        double z_score = (implied_vol - rv->rv_mean) / rv->rv_std;
        analysis.iv_percentile = 0.5 * (1.0 + erf(z_score / sqrt(2.0)));
        
        // Determine vol regime
        if (relevant_rv < rv->rv_mean - 0.5 * rv->rv_std) {
            analysis.vol_regime = 0;  // Low vol
        } else if (relevant_rv > rv->rv_mean + 0.5 * rv->rv_std) {
            analysis.vol_regime = 2;  // High vol
        } else {
            analysis.vol_regime = 1;  // Normal vol
        }
    }
    
    // Generate trading signals
    double spread_threshold = relevant_rv * 0.15;  // 15% threshold
    
    if (analysis.iv_rv_spread > spread_threshold) {
        strcpy(analysis.signal, "EXPENSIVE");
        if (analysis.iv_percentile > 0.8) {
            strcpy(analysis.recommendation, "SELL VOL - IV extremely rich vs RV");
        } else {
            strcpy(analysis.recommendation, "SHORT BIAS - IV moderately expensive");
        }
    } else if (analysis.iv_rv_spread < -spread_threshold) {
        strcpy(analysis.signal, "CHEAP");
        if (analysis.iv_percentile < 0.2) {
            strcpy(analysis.recommendation, "BUY VOL - IV extremely cheap vs RV");
        } else {
            strcpy(analysis.recommendation, "LONG BIAS - IV moderately cheap");
        }
    } else {
        strcpy(analysis.signal, "NEUTRAL");
        strcpy(analysis.recommendation, "FAIR VALUE - IV in line with RV");
    }
    
    // Add trend consideration
    if (rv->rv_trend > 0.2) {
        strcat(analysis.recommendation, " (RV rising)");
    } else if (rv->rv_trend < -0.2) {
        strcat(analysis.recommendation, " (RV falling)");
    }
    
    return analysis;
}