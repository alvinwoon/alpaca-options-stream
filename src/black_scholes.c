#include "../include/black_scholes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Standard normal cumulative distribution function
double standard_normal_cdf(double x) {
    // Approximation using erf function
    return 0.5 * (1.0 + erf(x / sqrt(2.0)));
}

// Standard normal probability density function
double standard_normal_pdf(double x) {
    return (1.0 / sqrt(2.0 * M_PI)) * exp(-0.5 * x * x);
}

// Calculate d1 parameter for Black-Scholes
static double calculate_d1(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0) return 0.0;
    return (log(S / K) + (r + 0.5 * sigma * sigma) * T) / (sigma * sqrt(T));
}

// Calculate d2 parameter for Black-Scholes
static double calculate_d2(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0) return 0.0;
    double d1 = calculate_d1(S, K, T, r, sigma);
    return d1 - sigma * sqrt(T);
}

// Black-Scholes call option price
double bs_call_price(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return fmax(S - K, 0.0);  // Intrinsic value at expiry
    if (sigma <= 0.0) return fmax(S - K * exp(-r * T), 0.0);
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    double d2 = calculate_d2(S, K, T, r, sigma);
    
    return S * standard_normal_cdf(d1) - K * exp(-r * T) * standard_normal_cdf(d2);
}

// Black-Scholes put option price
double bs_put_price(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return fmax(K - S, 0.0);  // Intrinsic value at expiry
    if (sigma <= 0.0) return fmax(K * exp(-r * T) - S, 0.0);
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    double d2 = calculate_d2(S, K, T, r, sigma);
    
    return K * exp(-r * T) * standard_normal_cdf(-d2) - S * standard_normal_cdf(-d1);
}

// Greeks calculations
double bs_delta_call(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return (S > K) ? 1.0 : 0.0;
    if (sigma <= 0.0) return (S > K * exp(-r * T)) ? 1.0 : 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    return standard_normal_cdf(d1);
}

double bs_delta_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return (S < K) ? -1.0 : 0.0;
    if (sigma <= 0.0) return (S < K * exp(-r * T)) ? -1.0 : 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    return standard_normal_cdf(d1) - 1.0;
}

double bs_gamma(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0) return 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    return standard_normal_pdf(d1) / (S * sigma * sqrt(T));
}

double bs_theta_call(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return 0.0;
    if (sigma <= 0.0) return (S > K * exp(-r * T)) ? r * K * exp(-r * T) : 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    double d2 = calculate_d2(S, K, T, r, sigma);
    
    double term1 = -(S * standard_normal_pdf(d1) * sigma) / (2.0 * sqrt(T));
    double term2 = -r * K * exp(-r * T) * standard_normal_cdf(d2);
    
    return term1 + term2;
}

double bs_theta_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return 0.0;
    if (sigma <= 0.0) return (S < K * exp(-r * T)) ? -r * K * exp(-r * T) : 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    double d2 = calculate_d2(S, K, T, r, sigma);
    
    double term1 = -(S * standard_normal_pdf(d1) * sigma) / (2.0 * sqrt(T));
    double term2 = r * K * exp(-r * T) * standard_normal_cdf(-d2);
    
    return term1 + term2;
}

double bs_vega(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0) return 0.0;
    
    double d1 = calculate_d1(S, K, T, r, sigma);
    return S * standard_normal_pdf(d1) * sqrt(T);
}

double bs_rho_call(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return 0.0;
    if (sigma <= 0.0) return (S > K * exp(-r * T)) ? K * T * exp(-r * T) : 0.0;
    
    double d2 = calculate_d2(S, K, T, r, sigma);
    return K * T * exp(-r * T) * standard_normal_cdf(d2);
}

double bs_rho_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0) return 0.0;
    if (sigma <= 0.0) return (S < K * exp(-r * T)) ? -K * T * exp(-r * T) : 0.0;
    
    double d2 = calculate_d2(S, K, T, r, sigma);
    return -K * T * exp(-r * T) * standard_normal_cdf(-d2);
}

// Parse expiry date and calculate time to expiry in years
double time_to_expiry_years(const char* expiry_date) {
    if (!expiry_date || strlen(expiry_date) < 6) return 0.0;
    
    // Parse YYMMDD format from option symbol
    int year = 2000 + (expiry_date[0] - '0') * 10 + (expiry_date[1] - '0');
    int month = (expiry_date[2] - '0') * 10 + (expiry_date[3] - '0');
    int day = (expiry_date[4] - '0') * 10 + (expiry_date[5] - '0');
    
    // Handle year 2000 issue - assume 20YY for years < 50, 19YY for years >= 50
    if (year < 2050) {
        // Already correct
    } else if (year >= 2050 && year < 2100) {
        year = 1900 + (year - 2000);
    }
    
    // Get current time
    time_t now = time(NULL);
    struct tm *now_tm = gmtime(&now);
    
    // Create expiry time structure
    struct tm expiry_tm = {0};
    expiry_tm.tm_year = year - 1900;
    expiry_tm.tm_mon = month - 1;
    expiry_tm.tm_mday = day;
    expiry_tm.tm_hour = 16;  // Options expire at 4 PM ET
    expiry_tm.tm_min = 0;
    expiry_tm.tm_sec = 0;
    
    time_t expiry_time = mktime(&expiry_tm);
    
    // Calculate difference in seconds and convert to years
    double seconds_diff = difftime(expiry_time, now);
    if (seconds_diff < 0) return 0.0;  // Already expired
    
    return seconds_diff / (365.25 * 24.0 * 3600.0);  // Convert to years
}

// Rational approximation for initial IV guess (Brenner & Subrahmanyam)
static double iv_initial_guess(double option_price, double S, double K, double T, int is_call) {
    double sqrt_T = sqrt(T);
    double sqrt_2pi = sqrt(2.0 * M_PI);
    
    // Forward price
    double F = S; // Assuming r ≈ 0 for initial guess
    
    if (is_call) {
        // For calls: σ ≈ √(2π/T) × (C/S)
        return sqrt_2pi / sqrt_T * (option_price / S);
    } else {
        // For puts: use put-call parity approximation
        double adjusted_price = option_price + S - K;
        return sqrt_2pi / sqrt_T * (adjusted_price / S);
    }
}

// Fallback bisection method (declare first)
static double implied_volatility_bisection(double option_price, double S, double K, double T, 
                                         double r, int is_call) {
    double vol_low = IV_MIN_VOL;
    double vol_high = IV_MAX_VOL;
    double vol_mid, price_mid;
    int iterations = 0;
    
    // Check bounds
    double price_low = is_call ? bs_call_price(S, K, T, r, vol_low) : 
                                bs_put_price(S, K, T, r, vol_low);
    double price_high = is_call ? bs_call_price(S, K, T, r, vol_high) : 
                                 bs_put_price(S, K, T, r, vol_high);
    
    if (option_price < price_low) return vol_low;
    if (option_price > price_high) return vol_high;
    
    // Bisection method
    while (iterations < IV_MAX_ITERATIONS && (vol_high - vol_low) > IV_TOLERANCE) {
        vol_mid = (vol_low + vol_high) / 2.0;
        
        price_mid = is_call ? bs_call_price(S, K, T, r, vol_mid) : 
                             bs_put_price(S, K, T, r, vol_mid);
        
        if (fabs(price_mid - option_price) < IV_TOLERANCE) {
            return vol_mid;
        }
        
        if (price_mid < option_price) {
            vol_low = vol_mid;
        } else {
            vol_high = vol_mid;
        }
        
        iterations++;
    }
    
    return (vol_low + vol_high) / 2.0;
}

// Corrado-Miller approximation for better initial guess
static double iv_corrado_miller_guess(double option_price, double S, double K, double T, double r, int is_call) {
    (void)is_call; // Suppress unused parameter warning
    
    double sqrt_T = sqrt(T);
    double sqrt_2pi = sqrt(2.0 * M_PI);
    
    // Discount factor
    double df = exp(-r * T);
    double F = S / df; // Forward price
    
    // Moneyness
    double x = log(F / K);
    
    // Corrado-Miller initial guess
    double n1 = sqrt_2pi / sqrt_T;
    double n2 = option_price - 0.5 * fabs(F - K);
    double n3 = (F + K) / 2.0;
    
    double guess = n1 * n2 / n3;
    
    // Second-order correction
    double correction = sqrt(pow(guess, 2) + 2.0 * fabs(x) / sqrt_T);
    
    return fmax(correction, IV_MIN_VOL);
}

// Newton-Raphson method for implied volatility (much faster than bisection)
double implied_volatility(double option_price, double S, double K, double T, 
                         double r, int is_call) {
    if (option_price <= 0.0 || S <= 0.0 || K <= 0.0 || T <= 0.0) {
        return 0.0;
    }
    
    // Check if option is at intrinsic value
    double intrinsic = is_call ? fmax(S - K, 0.0) : fmax(K - S, 0.0);
    if (option_price <= intrinsic + 1e-6) {
        return IV_MIN_VOL;  // Minimal volatility for at-intrinsic options
    }
    
    // Get intelligent initial guess using Corrado-Miller approximation
    double vol = iv_corrado_miller_guess(option_price, S, K, T, r, is_call);
    vol = fmax(fmin(vol, IV_MAX_VOL * 0.5), IV_MIN_VOL); // Bound initial guess
    
    int iterations = 0;
    double price_diff, vega, vol_new;
    
    // Newton-Raphson iteration: vol_new = vol - f(vol)/f'(vol)
    // where f(vol) = BS_price(vol) - market_price
    // and f'(vol) = vega
    while (iterations < IV_MAX_ITERATIONS) {
        // Calculate theoretical price and vega at current vol
        double theoretical_price = is_call ? bs_call_price(S, K, T, r, vol) : 
                                            bs_put_price(S, K, T, r, vol);
        double current_vega = bs_vega(S, K, T, r, vol);
        
        price_diff = theoretical_price - option_price;
        
        // Convergence check
        if (fabs(price_diff) < IV_TOLERANCE || current_vega < 1e-10) {
            break;
        }
        
        // Newton-Raphson update
        vol_new = vol - price_diff / current_vega;
        
        // Safeguard bounds
        vol_new = fmax(fmin(vol_new, IV_MAX_VOL), IV_MIN_VOL);
        
        // Check for convergence in volatility
        if (fabs(vol_new - vol) < IV_TOLERANCE) {
            vol = vol_new;
            break;
        }
        
        vol = vol_new;
        iterations++;
    }
    
    // Fallback to bisection if Newton-Raphson failed
    if (iterations >= IV_MAX_ITERATIONS) {
        return implied_volatility_bisection(option_price, S, K, T, r, is_call);
    }
    
    return vol;
}


// Calculate full Black-Scholes metrics for an option
bs_result_t calculate_full_bs_metrics(double S, double K, double T, double r, 
                                     double market_price, int is_call) {
    bs_result_t result = {0};
    
    // Calculate implied volatility first
    result.implied_vol = implied_volatility(market_price, S, K, T, r, is_call);
    result.iv_converged = (result.implied_vol > IV_MIN_VOL && 
                          result.implied_vol < IV_MAX_VOL) ? 1 : 0;
    
    // Use implied vol to calculate theoretical prices and Greeks
    double sigma = result.implied_vol;
    
    result.call_price = bs_call_price(S, K, T, r, sigma);
    result.put_price = bs_put_price(S, K, T, r, sigma);
    
    // Greeks
    result.delta = is_call ? bs_delta_call(S, K, T, r, sigma) : 
                            bs_delta_put(S, K, T, r, sigma);
    result.gamma = bs_gamma(S, K, T, r, sigma);
    result.theta = is_call ? bs_theta_call(S, K, T, r, sigma) : 
                            bs_theta_put(S, K, T, r, sigma);
    result.vega = bs_vega(S, K, T, r, sigma);
    result.rho = is_call ? bs_rho_call(S, K, T, r, sigma) : 
                          bs_rho_put(S, K, T, r, sigma);
    
    // 2nd order Greeks
    result.vanna = bs_vanna(S, K, T, r, sigma);
    result.charm = is_call ? bs_charm_call(S, K, T, r, sigma) : 
                            bs_charm_put(S, K, T, r, sigma);
    result.volga = bs_volga(S, K, T, r, sigma);
    
    // 3rd order Greeks
    result.speed = bs_speed(S, K, T, r, sigma);
    result.zomma = bs_zomma(S, K, T, r, sigma);
    result.color = is_call ? bs_color_call(S, K, T, r, sigma) : 
                            bs_color_put(S, K, T, r, sigma);
    
    return result;
}

// 2nd Order Greeks Implementations

// Vanna: ∂²V/∂S∂σ - Delta sensitivity to volatility
double bs_vanna(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    
    double vega = S * standard_normal_pdf(d1) * sqrt(T);
    double vanna = -vega * d2 / sigma;
    
    return vanna;
}

// Charm (Call): ∂²V/∂S∂T - Delta decay over time for calls
double bs_charm_call(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    
    double phi_d1 = standard_normal_pdf(d1);
    double sqrt_T = sqrt(T);
    
    double charm = -phi_d1 * (2*r*T - d2*sigma*sqrt_T) / (2*T*sigma*sqrt_T);
    
    return charm;
}

// Charm (Put): ∂²V/∂S∂T - Delta decay over time for puts  
double bs_charm_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    
    double phi_d1 = standard_normal_pdf(d1);
    double sqrt_T = sqrt(T);
    
    double charm = -phi_d1 * (2*r*T - d2*sigma*sqrt_T) / (2*T*sigma*sqrt_T) - r*exp(-r*T);
    
    return charm;
}

// Volga/Vomma: ∂²V/∂σ² - Vega sensitivity to volatility
double bs_volga(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    
    double vega = S * standard_normal_pdf(d1) * sqrt(T);
    double volga = vega * d1 * d2 / sigma;
    
    return volga;
}

// Speed: ∂³V/∂S³ - Gamma sensitivity to underlying price
double bs_speed(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double sqrt_T = sqrt(T);
    
    double phi_d1 = standard_normal_pdf(d1);
    double gamma = phi_d1 / (S * sigma * sqrt_T);
    
    // Speed = -Gamma/S * (d1/(σ√T) + 1)
    double speed = -gamma / S * (d1 / (sigma * sqrt_T) + 1.0);
    
    return speed;
}

// Zomma: ∂³V/∂S²∂σ - Gamma sensitivity to volatility  
double bs_zomma(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    double sqrt_T = sqrt(T);
    
    double phi_d1 = standard_normal_pdf(d1);
    double gamma = phi_d1 / (S * sigma * sqrt_T);
    
    // Zomma = Gamma * (d1*d2 - 1) / σ
    double zomma = gamma * (d1 * d2 - 1.0) / sigma;
    
    return zomma;
}

// Color (Call): ∂³V/∂S²∂T - Gamma decay over time for calls
double bs_color_call(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    double d1 = (log(S/K) + (r + 0.5*sigma*sigma)*T) / (sigma*sqrt(T));
    double d2 = d1 - sigma*sqrt(T);
    double sqrt_T = sqrt(T);
    
    double phi_d1 = standard_normal_pdf(d1);
    
    // Color = -∂Gamma/∂T = -phi(d1)/(2*S*T*σ*√T) * [2*r*T + 1 + d1*(2*r*T - d2*σ*√T)/(σ*√T)]
    double term1 = -phi_d1 / (2.0 * S * T * sigma * sqrt_T);
    double term2 = 2.0 * r * T + 1.0;
    double term3 = d1 * (2.0 * r * T - d2 * sigma * sqrt_T) / (sigma * sqrt_T);
    
    double color = term1 * (term2 + term3);
    
    return color;
}

// Color (Put): ∂³V/∂S²∂T - Gamma decay over time for puts
double bs_color_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0.0 || sigma <= 0.0 || S <= 0.0) return 0.0;
    
    // Color for puts is the same as calls since Gamma is identical for calls and puts
    return bs_color_call(S, K, T, r, sigma);
}