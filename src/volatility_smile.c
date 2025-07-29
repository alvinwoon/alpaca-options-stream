#include "../include/volatility_smile.h"
#include "../include/symbol_parser.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

void initialize_smile_analysis(smile_analysis_t *analysis) {
    memset(analysis, 0, sizeof(smile_analysis_t));
    analysis->last_analysis = time(NULL);
}

double calculate_moneyness(double strike, double underlying_price) {
    if (underlying_price <= 0.0) return 0.0;
    return strike / underlying_price;
}

void sort_smile_points_by_strike(smile_point_t *points, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (points[j].strike > points[j + 1].strike) {
                smile_point_t temp = points[j];
                points[j] = points[j + 1];
                points[j + 1] = temp;
            }
        }
    }
}

double interpolate_atm_vol(volatility_smile_t *smile, double underlying_price) {
    if (smile->point_count < 2) return 0.0;
    
    // Find the two points closest to ATM (moneyness = 1.0)
    double target_moneyness = 1.0;
    int best_idx = 0;
    double best_diff = fabs(smile->points[0].moneyness - target_moneyness);
    
    for (int i = 1; i < smile->point_count; i++) {
        double diff = fabs(smile->points[i].moneyness - target_moneyness);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }
    
    // If we found an exact match or close enough, return it
    if (best_diff < 0.01) {  // Within 1% of ATM
        return smile->points[best_idx].implied_vol;
    }
    
    // Otherwise, interpolate between two closest points
    if (best_idx > 0 && best_idx < smile->point_count - 1) {
        double x0 = smile->points[best_idx - 1].moneyness;
        double x1 = smile->points[best_idx + 1].moneyness;
        double y0 = smile->points[best_idx - 1].implied_vol;
        double y1 = smile->points[best_idx + 1].implied_vol;
        
        // Linear interpolation
        double t = (target_moneyness - x0) / (x1 - x0);
        return y0 + t * (y1 - y0);
    }
    
    return smile->points[best_idx].implied_vol;
}

double polynomial_fit_r_squared(smile_point_t *points, int count) {
    if (count < 3) return 0.0;
    
    // Simple R² calculation for linear fit to log(moneyness) vs IV
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    
    for (int i = 0; i < count; i++) {
        double x = log(points[i].moneyness);
        double y = points[i].implied_vol;
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
        sum_y2 += y * y;
    }
    
    double n = (double)count;
    double numerator = n * sum_xy - sum_x * sum_y;
    double denom_x = n * sum_x2 - sum_x * sum_x;
    double denom_y = n * sum_y2 - sum_y * sum_y;
    
    if (denom_x <= 0.0 || denom_y <= 0.0) return 0.0;
    
    double r = numerator / sqrt(denom_x * denom_y);
    return r * r;
}

void calculate_smile_metrics(volatility_smile_t *smile) {
    if (smile->point_count < MIN_SMILE_POINTS) {
        smile->sufficient_data = 0;
        return;
    }
    
    smile->sufficient_data = 1;
    
    // Sort points by strike for analysis
    sort_smile_points_by_strike(smile->points, smile->point_count);
    
    // Calculate basic statistics
    smile->min_vol = smile->points[0].implied_vol;
    smile->max_vol = smile->points[0].implied_vol;
    
    for (int i = 0; i < smile->point_count; i++) {
        double vol = smile->points[i].implied_vol;
        if (vol < smile->min_vol) smile->min_vol = vol;
        if (vol > smile->max_vol) smile->max_vol = vol;
    }
    
    // Calculate ATM volatility
    smile->atm_vol = interpolate_atm_vol(smile, smile->underlying_price);
    
    // Calculate fit quality
    smile->r_squared = polynomial_fit_r_squared(smile->points, smile->point_count);
    
    // Find OTM put and call volatilities for skew calculation
    double otm_put_vol = 0.0, otm_call_vol = 0.0;
    int found_otm_put = 0, found_otm_call = 0;
    
    for (int i = 0; i < smile->point_count; i++) {
        double moneyness = smile->points[i].moneyness;
        
        // OTM put: strike significantly below underlying (moneyness < 0.95)
        if (moneyness < 0.95 && smile->points[i].option_type == 'P') {
            otm_put_vol = smile->points[i].implied_vol;
            found_otm_put = 1;
        }
        
        // OTM call: strike significantly above underlying (moneyness > 1.05)
        if (moneyness > 1.05 && smile->points[i].option_type == 'C') {
            otm_call_vol = smile->points[i].implied_vol;
            found_otm_call = 1;
        }
    }
    
    // Calculate skew metrics
    if (found_otm_put && smile->atm_vol > 0) {
        smile->put_skew = smile->atm_vol - otm_put_vol;
    }
    
    if (found_otm_call && smile->atm_vol > 0) {
        smile->call_skew = otm_call_vol - smile->atm_vol;
    }
    
    // Estimate curvature (second derivative at ATM)
    smile->smile_curvature = 0.0;
    if (smile->point_count >= 3) {
        // Find three points around ATM for curvature calculation
        int atm_idx = smile->point_count / 2;  // Rough approximation
        if (atm_idx > 0 && atm_idx < smile->point_count - 1) {
            double h1 = smile->points[atm_idx].moneyness - smile->points[atm_idx - 1].moneyness;
            double h2 = smile->points[atm_idx + 1].moneyness - smile->points[atm_idx].moneyness;
            
            if (h1 > 0 && h2 > 0) {
                double y0 = smile->points[atm_idx - 1].implied_vol;
                double y1 = smile->points[atm_idx].implied_vol;
                double y2 = smile->points[atm_idx + 1].implied_vol;
                
                // Second derivative approximation
                smile->smile_curvature = (y2 - 2*y1 + y0) / (h1 * h2);
            }
        }
    }
}

void detect_smile_patterns(volatility_smile_t *smile) {
    if (!smile->sufficient_data) return;
    
    // Reset pattern flags
    smile->has_put_skew = 0;
    smile->has_call_skew = 0;
    smile->has_smile = 0;
    smile->is_inverted = 0;
    
    // Detect put skew (higher IV for lower strikes)
    if (smile->put_skew > SKEW_THRESHOLD) {
        smile->has_put_skew = 1;
    }
    
    // Detect call skew (higher IV for higher strikes)
    if (smile->call_skew > SKEW_THRESHOLD) {
        smile->has_call_skew = 1;
    }
    
    // Detect volatility smile (U-shape: higher IV at both ends)
    if (smile->smile_curvature > SMILE_THRESHOLD && 
        (smile->max_vol - smile->atm_vol) > SMILE_THRESHOLD) {
        smile->has_smile = 1;
    }
    
    // Detect inverted smile (lower IV at extremes)
    if (smile->smile_curvature < -SMILE_THRESHOLD && 
        (smile->atm_vol - smile->min_vol) > SMILE_THRESHOLD) {
        smile->is_inverted = 1;
    }
}

void analyze_volatility_smile(volatility_smile_t *smile) {
    calculate_smile_metrics(smile);
    detect_smile_patterns(smile);
    smile->last_update = time(NULL);
}

int is_smile_anomaly(volatility_smile_t *smile) {
    if (!smile->sufficient_data) return 0;
    
    // Check for unusual patterns that might indicate opportunities
    
    // Extreme skew (> 5% IV difference)
    if (fabs(smile->put_skew) > 0.05 || fabs(smile->call_skew) > 0.05) {
        return 1;
    }
    
    // Inverted smile (unusual market condition)
    if (smile->is_inverted) {
        return 1;
    }
    
    // Very low R² (poor fit, might indicate data issues or opportunities)
    if (smile->r_squared < 0.7 && smile->point_count >= 5) {
        return 1;
    }
    
    // Extreme volatility range (> 10% spread)
    if ((smile->max_vol - smile->min_vol) > 0.10) {
        return 1;
    }
    
    return 0;
}

void log_smile_opportunity(volatility_smile_t *smile, const char *pattern_type) {
    printf("\nVOLATILITY OPPORTUNITY DETECTED\n");
    printf("Pattern: %s\n", pattern_type);
    printf("Underlying: %s | Expiry: %s\n", smile->underlying, smile->expiry_date);
    printf("ATM Vol: %.1f%% | Put Skew: %.1f%% | Call Skew: %.1f%%\n",
           smile->atm_vol * 100, smile->put_skew * 100, smile->call_skew * 100);
    printf("Vol Range: %.1f%% - %.1f%% | Curvature: %.3f\n",
           smile->min_vol * 100, smile->max_vol * 100, smile->smile_curvature);
    printf("Fit Quality: R² = %.3f | Data Points: %d\n", 
           smile->r_squared, smile->point_count);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void display_smile_alerts(smile_analysis_t *analysis) {
    for (int i = 0; i < analysis->smile_count; i++) {
        volatility_smile_t *smile = &analysis->smiles[i];
        
        if (is_smile_anomaly(smile)) {
            if (smile->has_put_skew && fabs(smile->put_skew) > 0.03) {
                log_smile_opportunity(smile, "EXTREME PUT SKEW");
            }
            if (smile->has_call_skew && fabs(smile->call_skew) > 0.03) {
                log_smile_opportunity(smile, "EXTREME CALL SKEW");
            }
            if (smile->is_inverted) {
                log_smile_opportunity(smile, "INVERTED SMILE");
            }
            if (smile->r_squared < 0.5) {
                log_smile_opportunity(smile, "POOR FIT - POTENTIAL MISPRICING");
            }
        }
    }
}

void update_smile_data(smile_analysis_t *analysis, alpaca_client_t *client) {
    // Clear existing smile data
    analysis->smile_count = 0;
    
    // Group options by underlying and expiration
    for (int i = 0; i < client->data_count; i++) {
        option_data_t *opt = &client->option_data[i];
        
        if (!opt->analytics_valid || !opt->bs_analytics.iv_converged) continue;
        
        // Parse option details
        option_details_t details = parse_option_details(opt->symbol);
        if (!details.is_valid) continue;
        
        // Find or create smile for this underlying/expiration
        volatility_smile_t *smile = NULL;
        for (int j = 0; j < analysis->smile_count; j++) {
            if (strcmp(analysis->smiles[j].underlying, details.underlying) == 0 &&
                strcmp(analysis->smiles[j].expiry_date, details.expiry_date) == 0) {
                smile = &analysis->smiles[j];
                break;
            }
        }
        
        // Create new smile if not found
        if (!smile && analysis->smile_count < MAX_SYMBOLS) {
            smile = &analysis->smiles[analysis->smile_count];
            memset(smile, 0, sizeof(volatility_smile_t));
            
            strncpy(smile->underlying, details.underlying, sizeof(smile->underlying) - 1);
            strncpy(smile->expiry_date, details.expiry_date, sizeof(smile->expiry_date) - 1);
            smile->time_to_expiry = opt->time_to_expiry;
            smile->underlying_price = opt->underlying_price;
            
            analysis->smile_count++;
        }
        
        // Add point to smile
        if (smile && smile->point_count < MAX_SMILE_POINTS) {
            smile_point_t *point = &smile->points[smile->point_count];
            
            point->strike = details.strike;
            point->implied_vol = opt->bs_analytics.implied_vol;
            point->moneyness = calculate_moneyness(details.strike, opt->underlying_price);
            point->time_to_expiry = opt->time_to_expiry;
            point->option_type = details.option_type;
            point->data_quality = 1;  // Assume good quality for now
            
            smile->point_count++;
        }
    }
    
    // Analyze each smile
    for (int i = 0; i < analysis->smile_count; i++) {
        analyze_volatility_smile(&analysis->smiles[i]);
    }
    
    analysis->last_analysis = time(NULL);
}