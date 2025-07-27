#include "../include/display.h"
#include "../include/symbol_parser.h"
#include "../include/black_scholes.h"
#include "../include/volatility_smile.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[42m\033[30m"  // Green background, black text
#define COLOR_RED     "\033[41m\033[37m"  // Red background, white text
#define COLOR_NORMAL  "\033[0m"

// Helper function to format value with color and proper padding
void format_value_with_color(char *buffer, size_t buffer_size, double current_value, 
                           double previous_value, const char *format, double threshold, int field_width) {
    char value_str[32];
    
    // First format the value without color
    snprintf(value_str, sizeof(value_str), format, current_value);
    
    if (fabs(current_value - previous_value) < threshold) {
        // No significant change - just pad to field width
        snprintf(buffer, buffer_size, "%-*s", field_width, value_str);
    } else if (current_value > previous_value) {
        // Value increased - green background with proper padding
        snprintf(buffer, buffer_size, "%s%-*s%s", COLOR_GREEN, field_width, value_str, COLOR_RESET);
    } else {
        // Value decreased - red background with proper padding
        snprintf(buffer, buffer_size, "%s%-*s%s", COLOR_RED, field_width, value_str, COLOR_RESET);
    }
}

// Helper function to update previous values (only for colored fields)
void update_previous_values(option_data_t *data) {
    // Calculate and store current spread
    if (data->has_quote && data->ask_price > 0 && data->bid_price > 0) {
        data->prev_spread = data->ask_price - data->bid_price;
    }
    
    // Store Greeks and IV if analytics are valid
    if (data->analytics_valid) {
        data->prev_implied_vol = data->bs_analytics.implied_vol;
        data->prev_delta = data->bs_analytics.delta;
        data->prev_gamma = data->bs_analytics.gamma;
        data->prev_theta = data->bs_analytics.theta;
        data->prev_vega = data->bs_analytics.vega;
        // Store 2nd and 3rd order Greeks
        data->prev_vanna = data->bs_analytics.vanna;
        data->prev_charm = data->bs_analytics.charm;
        data->prev_volga = data->bs_analytics.volga;
        data->prev_speed = data->bs_analytics.speed;
        data->prev_zomma = data->bs_analytics.zomma;
        data->prev_color = data->bs_analytics.color;
    }
}

// Static buffer for previous display state to detect changes
static option_data_t prev_display_data[MAX_SYMBOLS];
static int prev_data_count = 0;
static int first_display = 1;

// Check if display data has changed significantly
static int has_display_changed(option_data_t *current_data, int current_count) {
    if (first_display || current_count != prev_data_count) {
        return 1;
    }
    
    for (int i = 0; i < current_count; i++) {
        option_data_t *curr = &current_data[i];
        option_data_t *prev = &prev_display_data[i];
        
        // Check for significant changes in key fields
        if (strcmp(curr->symbol, prev->symbol) != 0 ||
            fabs(curr->last_price - prev->last_price) > 0.001 ||
            fabs(curr->bid_price - prev->bid_price) > 0.001 ||
            fabs(curr->ask_price - prev->ask_price) > 0.001 ||
            fabs(curr->underlying_price - prev->underlying_price) > 0.01 ||
            curr->analytics_valid != prev->analytics_valid) {
            return 1;
        }
        
        // Check for Greeks changes if analytics are valid
        if (curr->analytics_valid && prev->analytics_valid) {
            if (fabs(curr->bs_analytics.implied_vol - prev->bs_analytics.implied_vol) > 0.001 ||
                fabs(curr->bs_analytics.delta - prev->bs_analytics.delta) > 0.001 ||
                fabs(curr->bs_analytics.gamma - prev->bs_analytics.gamma) > 0.001) {
                return 1;
            }
        }
    }
    
    return 0;
}

// Display thread function - runs independently of WebSocket messages
static void* display_thread_func(void *arg) {
    alpaca_client_t *client = (alpaca_client_t*)arg;
    
    while (client->display_running) {
        // Lock the data mutex while reading
        pthread_mutex_lock(&client->data_mutex);
        
        // Make a local copy of the data to minimize lock time
        int data_count = client->data_count;
        option_data_t local_data[MAX_SYMBOLS];
        memcpy(local_data, client->option_data, sizeof(option_data_t) * data_count);
        
        pthread_mutex_unlock(&client->data_mutex);
        
        // Only display if we have data and something has changed
        if (data_count > 0 && has_display_changed(local_data, data_count)) {
            // Temporarily copy data back to client for display function
            pthread_mutex_lock(&client->data_mutex);
            display_option_data(client);
            
            // Perform volatility smile analysis every 10 seconds
            static time_t last_smile_analysis = 0;
            time_t current_time = time(NULL);
            if (client->smile_analysis && (current_time - last_smile_analysis >= 10)) {
                update_smile_data((smile_analysis_t*)client->smile_analysis, client);
                display_smile_alerts((smile_analysis_t*)client->smile_analysis);
                last_smile_analysis = current_time;
            }
            
            pthread_mutex_unlock(&client->data_mutex);
            
            // Update previous state
            memcpy(prev_display_data, local_data, sizeof(option_data_t) * data_count);
            prev_data_count = data_count;
            first_display = 0;
        }
        
        // Sleep for a shorter interval to be more responsive, but only update when needed
        usleep(250000); // 250ms instead of 1 second for better responsiveness
    }
    
    return NULL;
}

void display_option_data(alpaca_client_t *client) {
    // Use more efficient screen clearing - save cursor, clear, restore position
    printf("\033[s");           // Save cursor position
    printf("\033[H");           // Move cursor to home position
    printf("\033[2J");          // Clear entire screen 
    printf("\033[?25l");        // Hide cursor during update
    fflush(stdout);             // Force flush before printing new content
    
    printf("=== Alpaca Options Live Data with Greeks ===\n");
    printf("Risk-free rate: %.2f%% | Symbols: %d | Press Ctrl+C to exit\n\n", 
           client->risk_free_rate * 100.0, client->data_count);
    
    // Header line 1: Basic option info and pricing
    printf("%-28s %-8s %-10s %-10s %-8s", 
           "OPTION CONTRACT", "UND.$", "LAST", "BID/ASK", "SPREAD");
    printf(" %-8s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s\n",
           "IV", "DELTA", "GAMMA", "THETA", "VEGA", "VANNA", "CHARM", "VOLGA", "SPEED", "ZOMMA", "COLOR");
    
    // Header line 2: Separators
    printf("%-28s %-8s %-10s %-10s %-8s", 
           "----------------------------", "--------", "----------", "----------", "--------");
    printf(" %-8s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s %-7s\n",
           "--------", "-------", "-------", "-------", "-------", "-------", "-------", "-------", "-------", "-------", "-------");
    
    for (int i = 0; i < client->data_count; i++) {
        option_data_t *data = &client->option_data[i];
        
        // Format symbol in readable format
        char readable_symbol[64];
        parse_option_symbol(data->symbol, readable_symbol, sizeof(readable_symbol));
        
        // Truncate symbol if too long
        if (strlen(readable_symbol) > 27) {
            readable_symbol[27] = '\0';
        }
        
        // Format underlying price (no color - clean)
        char und_str[16];
        strcpy(und_str, "N/A");  // Ensure clean initialization
        if (data->analytics_valid && data->underlying_price > 0) {
            snprintf(und_str, sizeof(und_str), "%.2f", data->underlying_price);
        }
        
        // Format last trade (no color - clean)
        char trade_str[16];
        strcpy(trade_str, "N/A");  // Ensure clean initialization
        if (data->has_trade) {
            snprintf(trade_str, sizeof(trade_str), "%.4f", data->last_price);
        }
        
        // Format bid/ask (no color - clean)
        char bid_ask_str[16];
        strcpy(bid_ask_str, "N/A");  // Ensure clean initialization
        if (data->has_quote && data->bid_price > 0 && data->ask_price > 0) {
            snprintf(bid_ask_str, sizeof(bid_ask_str), "%.3f/%.3f", data->bid_price, data->ask_price);
        }
        
        // Format spread with color
        char spread_str[32];
        strcpy(spread_str, "N/A     ");  // Clean initialization with padding (8 chars)
        if (data->has_quote && data->ask_price > 0 && data->bid_price > 0) {
            double spread = data->ask_price - data->bid_price;
            if (data->prev_spread > 0) {
                format_value_with_color(spread_str, sizeof(spread_str), spread, 
                                      data->prev_spread, "%.3f", 0.001, 8);
            } else {
                snprintf(spread_str, sizeof(spread_str), "%-8.3f", spread);
            }
        }
        
        // Format Greeks and IV with color coding
        char iv_str[24];
        char delta_str[24];
        char gamma_str[24];
        char theta_str[24];
        char vega_str[24];
        char vanna_str[24];
        char charm_str[24];
        char volga_str[24];
        char speed_str[24];
        char zomma_str[24];
        char color_str[24];
        
        // Clean initialization for all Greeks with proper padding
        strcpy(iv_str, "N/A     ");      // 8 chars total
        strcpy(delta_str, "N/A    ");    // 7 chars total  
        strcpy(gamma_str, "N/A    ");    // 7 chars total
        strcpy(theta_str, "N/A    ");    // 7 chars total
        strcpy(vega_str, "N/A    ");     // 7 chars total
        strcpy(vanna_str, "N/A    ");    // 7 chars total
        strcpy(charm_str, "N/A    ");    // 7 chars total
        strcpy(volga_str, "N/A    ");    // 7 chars total
        strcpy(speed_str, "N/A    ");    // 7 chars total
        strcpy(zomma_str, "N/A    ");    // 7 chars total
        strcpy(color_str, "N/A    ");    // 7 chars total
        
        if (data->analytics_valid && data->bs_analytics.iv_converged) {
            // Implied Volatility as percentage with color
            if (data->prev_implied_vol > 0) {
                format_value_with_color(iv_str, sizeof(iv_str), data->bs_analytics.implied_vol * 100.0, 
                                      data->prev_implied_vol * 100.0, "%.1f%%", 0.1, 8);
            } else {
                snprintf(iv_str, sizeof(iv_str), "%-8.1f%%", data->bs_analytics.implied_vol * 100.0);
            }
            
            // Delta with color
            double current_delta = data->bs_analytics.delta * DELTA_SCALE;
            double prev_delta = data->prev_delta * DELTA_SCALE;
            if (data->prev_delta != 0) {
                format_value_with_color(delta_str, sizeof(delta_str), current_delta, prev_delta, "%.3f", 0.001, 7);
            } else {
                snprintf(delta_str, sizeof(delta_str), "%-7.3f", current_delta);
            }
            
            // Gamma with color
            double current_gamma = data->bs_analytics.gamma * GAMMA_SCALE;
            double prev_gamma = data->prev_gamma * GAMMA_SCALE;
            if (data->prev_gamma != 0) {
                format_value_with_color(gamma_str, sizeof(gamma_str), current_gamma, prev_gamma, "%.3f", 0.001, 7);
            } else {
                snprintf(gamma_str, sizeof(gamma_str), "%-7.3f", current_gamma);
            }
            
            // Theta with color
            double current_theta = data->bs_analytics.theta * THETA_SCALE;
            double prev_theta = data->prev_theta * THETA_SCALE;
            if (data->prev_theta != 0) {
                format_value_with_color(theta_str, sizeof(theta_str), current_theta, prev_theta, "%.3f", 0.001, 7);
            } else {
                snprintf(theta_str, sizeof(theta_str), "%-7.3f", current_theta);
            }
            
            // Vega with color
            double current_vega = data->bs_analytics.vega / VEGA_SCALE;
            double prev_vega = data->prev_vega / VEGA_SCALE;
            if (data->prev_vega != 0) {
                format_value_with_color(vega_str, sizeof(vega_str), current_vega, prev_vega, "%.3f", 0.001, 7);
            } else {
                snprintf(vega_str, sizeof(vega_str), "%-7.3f", current_vega);
            }
            
            // 2nd Order Greeks with color
            double current_vanna = data->bs_analytics.vanna / 100.0;
            double prev_vanna = data->prev_vanna / 100.0;
            if (data->prev_vanna != 0) {
                format_value_with_color(vanna_str, sizeof(vanna_str), current_vanna, prev_vanna, "%.3f", 0.001, 7);
            } else {
                snprintf(vanna_str, sizeof(vanna_str), "%-7.3f", current_vanna);
            }
            
            double current_charm = data->bs_analytics.charm * 365.0;
            double prev_charm = data->prev_charm * 365.0;
            if (data->prev_charm != 0) {
                format_value_with_color(charm_str, sizeof(charm_str), current_charm, prev_charm, "%.1f", 0.1, 7);
            } else {
                snprintf(charm_str, sizeof(charm_str), "%-7.1f", current_charm);
            }
            
            double current_volga = data->bs_analytics.volga / 100.0;
            double prev_volga = data->prev_volga / 100.0;
            if (data->prev_volga != 0) {
                format_value_with_color(volga_str, sizeof(volga_str), current_volga, prev_volga, "%.3f", 0.001, 7);
            } else {
                snprintf(volga_str, sizeof(volga_str), "%-7.3f", current_volga);
            }
            
            // 3rd Order Greeks with color
            double current_speed = data->bs_analytics.speed * 1000.0;
            double prev_speed = data->prev_speed * 1000.0;
            if (data->prev_speed != 0) {
                format_value_with_color(speed_str, sizeof(speed_str), current_speed, prev_speed, "%.4f", 0.0001, 7);
            } else {
                snprintf(speed_str, sizeof(speed_str), "%-7.4f", current_speed);
            }
            
            double current_zomma = data->bs_analytics.zomma / 100.0;
            double prev_zomma = data->prev_zomma / 100.0;
            if (data->prev_zomma != 0) {
                format_value_with_color(zomma_str, sizeof(zomma_str), current_zomma, prev_zomma, "%.3f", 0.001, 7);
            } else {
                snprintf(zomma_str, sizeof(zomma_str), "%-7.3f", current_zomma);
            }
            
            double current_color = data->bs_analytics.color * 365.0;
            double prev_color = data->prev_color * 365.0;
            if (data->prev_color != 0) {
                format_value_with_color(color_str, sizeof(color_str), current_color, prev_color, "%.1f", 0.1, 7);
            } else {
                snprintf(color_str, sizeof(color_str), "%-7.1f", current_color);
            }
        }
        
        // Print the row with explicit color reset after each field
        printf("%-28s" COLOR_RESET " %-8s" COLOR_RESET " %-10s" COLOR_RESET " %-10s" COLOR_RESET " %-8s" COLOR_RESET, 
               readable_symbol, und_str, trade_str, bid_ask_str, spread_str);
        printf(" %-8s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET " %-7s" COLOR_RESET "\n",
               iv_str, delta_str, gamma_str, theta_str, vega_str, vanna_str, charm_str, volga_str, speed_str, zomma_str, color_str);
        
        // Update previous values for next comparison
        update_previous_values(data);
    }
    
    printf("\nGreeks: Delta, Gamma(/$1), Theta(/day), Vega(/1%%vol) | IV=Implied Volatility\n");
    printf("2nd Order: Vanna(/100), Charm(×365), Volga(/100) | 3rd Order: Speed(/$1000), Zomma(/100), Color(×365)\n");
    printf("Colors: " COLOR_GREEN " GREEN " COLOR_RESET " = Up, " COLOR_RED " RED " COLOR_RESET " = Down\n");
    
    // Display volatility smile summary if available
    if (client->smile_analysis) {
        smile_analysis_t *analysis = (smile_analysis_t*)client->smile_analysis;
        int total_smiles = 0, put_skews = 0, call_skews = 0, anomalies = 0;
        
        for (int i = 0; i < analysis->smile_count; i++) {
            volatility_smile_t *smile = &analysis->smiles[i];
            if (smile->sufficient_data) {
                total_smiles++;
                if (smile->has_put_skew) put_skews++;
                if (smile->has_call_skew) call_skews++;
                if (is_smile_anomaly(smile)) anomalies++;
            }
        }
        
        if (total_smiles > 0) {
            printf("Vol Smiles: %d analyzed | Put Skews: %d | Call Skews: %d | ", 
                   total_smiles, put_skews, call_skews);
            if (anomalies > 0) {
                printf(COLOR_RED "🚨 %d ANOMALIES DETECTED 🚨" COLOR_RESET "\n", anomalies);
            } else {
                printf("Anomalies: %d\n", anomalies);
            }
        }
    }
    
    printf("Live streaming... (data updates in real-time)\n");
    
    // Show cursor again and ensure output is flushed
    printf("\033[?25h");        // Show cursor
    fflush(stdout);
}

void display_symbols_list(alpaca_client_t *client, const char *title) {
    printf("%s:\n", title);
    for (int i = 0; i < client->symbol_count; i++) {
        char readable_symbol[64];
        parse_option_symbol(client->symbols[i], readable_symbol, sizeof(readable_symbol));
        printf("  %s (%s)\n", readable_symbol, client->symbols[i]);
    }
    printf("\n");
}

// Start the display thread
int start_display_thread(alpaca_client_t *client) {
    // Initialize mutex
    if (pthread_mutex_init(&client->data_mutex, NULL) != 0) {
        printf("Failed to initialize data mutex\n");
        return 0;
    }
    
    // Set display running flag
    client->display_running = 1;
    
    // Create the display thread
    if (pthread_create(&client->display_thread, NULL, display_thread_func, client) != 0) {
        printf("Failed to create display thread\n");
        pthread_mutex_destroy(&client->data_mutex);
        client->display_running = 0;
        return 0;
    }
    
    printf("Display thread started (refresh interval: %d seconds)\n", client->display_interval_seconds);
    return 1;
}

// Stop the display thread
void stop_display_thread(alpaca_client_t *client) {
    if (!client->display_running) return;
    
    // Signal the thread to stop
    client->display_running = 0;
    
    // Wait for the thread to finish
    pthread_join(client->display_thread, NULL);
    
    // Destroy the mutex
    pthread_mutex_destroy(&client->data_mutex);
    
    printf("Display thread stopped\n");
}