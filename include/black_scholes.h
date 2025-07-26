#ifndef BLACK_SCHOLES_H
#define BLACK_SCHOLES_H

#include <math.h>

// Black-Scholes pricing and Greeks functions
typedef struct {
    double call_price;
    double put_price;
    double delta;
    double gamma;
    double theta;
    double vega;
    double rho;
    double implied_vol;
    int iv_converged;  // 1 if IV calculation converged, 0 otherwise
    // 2nd order Greeks
    double vanna;      // ∂²V/∂S∂σ - Delta sensitivity to volatility
    double charm;      // ∂²V/∂S∂T - Delta decay over time  
    double volga;      // ∂²V/∂σ² - Vega sensitivity to volatility
    // 3rd order Greeks
    double speed;      // ∂³V/∂S³ - Gamma sensitivity to underlying price
    double zomma;      // ∂³V/∂S²∂σ - Gamma sensitivity to volatility
    double color;      // ∂³V/∂S²∂T - Gamma decay over time
} bs_result_t;

// Core Black-Scholes functions
double bs_call_price(double S, double K, double T, double r, double sigma);
double bs_put_price(double S, double K, double T, double r, double sigma);

// Greeks calculations
double bs_delta_call(double S, double K, double T, double r, double sigma);
double bs_delta_put(double S, double K, double T, double r, double sigma);
double bs_gamma(double S, double K, double T, double r, double sigma);
double bs_theta_call(double S, double K, double T, double r, double sigma);
double bs_theta_put(double S, double K, double T, double r, double sigma);
double bs_vega(double S, double K, double T, double r, double sigma);
double bs_rho_call(double S, double K, double T, double r, double sigma);
double bs_rho_put(double S, double K, double T, double r, double sigma);

// 2nd order Greeks
double bs_vanna(double S, double K, double T, double r, double sigma);
double bs_charm_call(double S, double K, double T, double r, double sigma);
double bs_charm_put(double S, double K, double T, double r, double sigma);
double bs_volga(double S, double K, double T, double r, double sigma);

// 3rd order Greeks
double bs_speed(double S, double K, double T, double r, double sigma);
double bs_zomma(double S, double K, double T, double r, double sigma);
double bs_color_call(double S, double K, double T, double r, double sigma);
double bs_color_put(double S, double K, double T, double r, double sigma);

// Implied volatility calculation
double implied_volatility(double option_price, double S, double K, double T, 
                         double r, int is_call);

// Enhanced analysis functions
bs_result_t calculate_full_bs_metrics(double S, double K, double T, double r, 
                                     double market_price, int is_call);

// Utility functions
double standard_normal_cdf(double x);
double standard_normal_pdf(double x);
double time_to_expiry_years(const char* expiry_date);

// Volatility analysis constants
#define IV_MAX_ITERATIONS 100
#define IV_TOLERANCE 1e-6
#define IV_MIN_VOL 0.001   // 0.1% minimum vol
#define IV_MAX_VOL 5.0     // 500% maximum vol

// Greeks display formatting
#define DELTA_SCALE 1.0    // Display delta as-is (0-1 for calls, -1-0 for puts)
#define GAMMA_SCALE 100.0  // Display gamma per $1 move
#define THETA_SCALE 365.0  // Display theta per day
#define VEGA_SCALE 100.0   // Display vega per 1% vol change

#endif // BLACK_SCHOLES_H