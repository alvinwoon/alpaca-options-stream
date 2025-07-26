#ifndef FRED_API_H
#define FRED_API_H

#include "types.h"

// FRED API configuration  
#define FRED_BASE_URL "https://api.stlouisfed.org/fred/series/observations"
#define DEFAULT_RISK_FREE_RATE 0.05  // 5% fallback rate

// Treasury rate series IDs (FRED series)
#define FRED_3_MONTH_TREASURY "DGS3MO"    // 3-Month Treasury Constant Maturity Rate
#define FRED_10_YEAR_TREASURY "DGS10"     // 10-Year Treasury Constant Maturity Rate
#define FRED_FEDERAL_FUNDS "FEDFUNDS"     // Federal Funds Effective Rate

// Risk-free rate fetching
int fetch_risk_free_rate(double *rate, const char *api_key);
int fetch_fred_rate(const char *series_id, double *rate, const char *api_key);

// Utility functions
double get_risk_free_rate_for_expiry(double time_to_expiry, const char *api_key);
const char* select_treasury_series(double time_to_expiry);

#endif // FRED_API_H