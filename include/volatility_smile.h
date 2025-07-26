#ifndef VOLATILITY_SMILE_H
#define VOLATILITY_SMILE_H

#include "types.h"

#define MAX_SMILE_POINTS 50
#define MIN_SMILE_POINTS 3
#define SKEW_THRESHOLD 0.02    // 2% IV difference threshold for skew detection
#define SMILE_THRESHOLD 0.01   // 1% IV difference threshold for smile detection

// Volatility smile data structures
typedef struct {
    double strike;
    double implied_vol;
    double moneyness;          // strike / underlying_price
    double time_to_expiry;
    char option_type;          // 'C' or 'P'
    int data_quality;          // 1 = good, 0 = questionable
} smile_point_t;

typedef struct {
    char underlying[16];
    char expiry_date[16];
    double time_to_expiry;
    double underlying_price;
    double atm_vol;            // At-the-money volatility
    
    smile_point_t points[MAX_SMILE_POINTS];
    int point_count;
    
    // Smile characteristics
    double put_skew;           // IV difference: ATM - OTM Put
    double call_skew;          // IV difference: OTM Call - ATM
    double smile_curvature;    // Second derivative at ATM
    double min_vol;            // Minimum IV in the smile
    double max_vol;            // Maximum IV in the smile
    
    // Pattern flags
    int has_put_skew;          // 1 if significant put skew detected
    int has_call_skew;         // 1 if significant call skew detected
    int has_smile;             // 1 if volatility smile detected
    int is_inverted;           // 1 if inverted smile (higher IV at ATM)
    
    // Quality metrics
    double r_squared;          // Fit quality of polynomial approximation
    int sufficient_data;       // 1 if enough points for reliable analysis
    
    time_t last_update;
} volatility_smile_t;

typedef struct smile_analysis_s {
    volatility_smile_t smiles[MAX_SYMBOLS];
    int smile_count;
    
    // Cross-expiration analysis
    double term_structure_slope;  // IV slope across time
    int backwardation;           // 1 if shorter-term IV > longer-term IV
    
    time_t last_analysis;
} smile_analysis_t;

// Function declarations
void initialize_smile_analysis(smile_analysis_t *analysis);
void update_smile_data(smile_analysis_t *analysis, alpaca_client_t *client);
void analyze_volatility_smile(volatility_smile_t *smile);
void detect_smile_patterns(volatility_smile_t *smile);
void calculate_smile_metrics(volatility_smile_t *smile);
double interpolate_atm_vol(volatility_smile_t *smile, double underlying_price);
void display_smile_alerts(smile_analysis_t *analysis);
int is_smile_anomaly(volatility_smile_t *smile);
void log_smile_opportunity(volatility_smile_t *smile, const char *pattern_type);

// Utility functions
double calculate_moneyness(double strike, double underlying_price);
double polynomial_fit_r_squared(smile_point_t *points, int count);
void sort_smile_points_by_strike(smile_point_t *points, int count);

#endif // VOLATILITY_SMILE_H