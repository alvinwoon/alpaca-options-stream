#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include "../include/types.h"
#include "../include/websocket.h"
#include "../include/stock_websocket.h"
#include "../include/api_client.h"
#include "../include/display.h"
#include "../include/mock_data.h"
#include "../include/fred_api.h"
#include "../include/volatility_smile.h"
#include "../include/config.h"
#include "../include/realized_vol.h"

static alpaca_client_t client = {0};
static int mock_mode = 0;

static void sigint_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    client.interrupted = 1;
    if (mock_mode) {
        stop_mock_data_stream();
    }
    stop_display_thread(&client);
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS] [ARGS...]\n", prog_name);
    printf("\nAPI Configuration:\n");
    printf("  API keys are read from 'config.json' file (see --setup for help)\n\n");
    printf("Modes:\n");
    printf("1. Direct symbols: %s SYMBOL1 SYMBOL2 ...\n", prog_name);
    printf("   Example: %s AAPL251220C00150000 AAPL251220P00150000\n", prog_name);
    printf("\n2. Auto-fetch mode (dates only): %s UNDERLYING EXP_DATE_GTE EXP_DATE_LTE\n", prog_name);
    printf("   Example: %s AAPL 2025-12-20 2025-12-20\n", prog_name);
    printf("\n3. Auto-fetch mode (dates + strikes): %s UNDERLYING EXP_DATE_GTE EXP_DATE_LTE STRIKE_GTE STRIKE_LTE\n", prog_name);
    printf("   Example: %s AAPL 2025-12-20 2025-12-20 150.00 160.00\n", prog_name);
    printf("\n4. Mock mode (for development): %s --mock SYMBOL1 SYMBOL2 ...\n", prog_name);
    printf("   Example: %s --mock AAPL251220C00150000 AAPL251220P00150000\n", prog_name);
    printf("\nOptions:\n");
    printf("  --mock           Use mock data (no API keys required)\n");
    printf("  --setup          Show API configuration help\n");
    printf("  --help, -h       Show this help\n");
    printf("\nNote: Use 0 for STRIKE_GTE or STRIKE_LTE to skip that filter\n");
}

static int parse_arguments(int argc, char **argv, app_config_t *config) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }
    
    // Handle help and setup options
    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(argv[1], "--setup") == 0) {
        print_config_help();
        return 0;
    }
    
    // Check for mock mode
    if (strcmp(argv[1], "--mock") == 0) {
        if (argc < 3) {
            printf("Error: Mock mode requires at least one symbol\n");
            print_usage(argv[0]);
            return 0;
        }
        
        mock_mode = 1;
        // Use real API keys even in mock mode for historical data fetching
        if (config->valid) {
            client.api_key = config->alpaca_api_key;
            client.api_secret = config->alpaca_api_secret;
        } else {
            client.api_key = "mock_key";
            client.api_secret = "mock_secret";
        }
        
        // Parse symbols for mock mode
        client.symbol_count = argc - 2;
        if (client.symbol_count > MAX_SYMBOLS) {
            client.symbol_count = MAX_SYMBOLS;
        }
        for (int i = 0; i < client.symbol_count; i++) {
            strncpy(client.symbols[i], argv[i + 2], sizeof(client.symbols[i]) - 1);
            client.symbols[i][sizeof(client.symbols[i]) - 1] = '\0';
        }
        printf("Mock mode: generating data for %d symbols\n", client.symbol_count);
        return 1;
    }
    
    // For non-mock modes, we need API credentials
    if (!config->valid) {
        printf("Error: API configuration required for non-mock mode\n");
        print_config_help();
        return 0;
    }
    
    // Set API credentials from config
    client.api_key = config->alpaca_api_key;
    client.api_secret = config->alpaca_api_secret;
    
    // Parse different argument patterns
    if (argc == 4 || argc == 6) {
        // Auto-fetch mode: dates only (4 args) or dates + strikes (6 args)
        char *underlying = argv[1];
        char *exp_date_gte = argv[2];
        char *exp_date_lte = argv[3];
        double strike_price_gte = 0.0;
        double strike_price_lte = 0.0;
        
        // Parse strike prices if provided
        if (argc == 6) {
            strike_price_gte = atof(argv[4]);
            strike_price_lte = atof(argv[5]);
        }
        
        // Check if dates are in YYYY-MM-DD format (basic validation)
        if (strlen(exp_date_gte) == 10 && exp_date_gte[4] == '-' && exp_date_gte[7] == '-' &&
            strlen(exp_date_lte) == 10 && exp_date_lte[4] == '-' && exp_date_lte[7] == '-') {
            
            printf("=== Auto-fetching option symbols ===\n");
            if (!fetch_option_symbols(&client, underlying, exp_date_gte, exp_date_lte, 
                                    strike_price_gte, strike_price_lte)) {
                printf("Failed to fetch option symbols\n");
                return 0;
            }
            
            if (client.symbol_count == 0) {
                printf("No option symbols found for the specified criteria\n");
                return 0;
            }
            
            printf("\n=== Starting WebSocket stream ===\n");
        } else {
            // Treat as direct symbols mode
            client.symbol_count = argc - 1;
            if (client.symbol_count > MAX_SYMBOLS) {
                client.symbol_count = MAX_SYMBOLS;
            }
            for (int i = 0; i < client.symbol_count; i++) {
                strncpy(client.symbols[i], argv[i + 1], sizeof(client.symbols[i]) - 1);
                client.symbols[i][sizeof(client.symbols[i]) - 1] = '\0';
            }
            printf("Direct symbols mode: streaming %d symbols\n", client.symbol_count);
        }
    } else if (argc > 1) {
        // Direct symbols mode
        client.symbol_count = argc - 1;
        if (client.symbol_count > MAX_SYMBOLS) {
            client.symbol_count = MAX_SYMBOLS;
        }
        for (int i = 0; i < client.symbol_count; i++) {
            strncpy(client.symbols[i], argv[i + 1], sizeof(client.symbols[i]) - 1);
            client.symbols[i][sizeof(client.symbols[i]) - 1] = '\0';
        }
        printf("Direct symbols mode: streaming %d symbols\n", client.symbol_count);
    } else {
        printf("Error: No symbols provided\n");
        print_usage(argv[0]);
        return 0;
    }
    
    return 1;
}

int main(int argc, char **argv) {
    // Load API configuration
    app_config_t config;
    load_config(&config);  // This may fail, but that's ok for mock mode
    
    // Create example config if it doesn't exist
    create_example_config();
    
    // Parse command line arguments
    if (!parse_arguments(argc, argv, &config)) {
        return 1;
    }
    
    printf("=== Alpaca Options Stream Parser ===\n");
    
    // Initialize curl early for both FRED API and WebSocket connections
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Fetch current risk-free rate (one-time call)
    printf("Fetching current risk-free rate...\n");
    double fred_rate_percent;
    const char *fred_key = (config.valid && strlen(config.fred_api_key) > 0) ? config.fred_api_key : NULL;
    if (fetch_risk_free_rate(&fred_rate_percent, fred_key)) {
        client.risk_free_rate = fred_rate_percent / 100.0; // Convert to decimal
        printf("Risk-free rate: %.4f%% (%.6f decimal)\n", fred_rate_percent, client.risk_free_rate);
    } else {
        client.risk_free_rate = DEFAULT_RISK_FREE_RATE;
        printf("Using default risk-free rate: %.4f%% (%.6f decimal)\n", 
               DEFAULT_RISK_FREE_RATE * 100, client.risk_free_rate);
    }
    
    // Initialize display threading (1 second interval)
    client.display_interval_seconds = 1;
    client.display_running = 0;
    
    // Initialize volatility smile analysis
    static smile_analysis_t smile_analysis;
    initialize_smile_analysis(&smile_analysis);
    client.smile_analysis = &smile_analysis;
    
    // Initialize realized volatility manager and fetch historical data
    client.rv_manager = init_rv_manager();
    
    // Extract unique underlying symbols and fetch their historical data
    char underlying_symbols[MAX_SYMBOLS][16];
    int underlying_count = 0;
    
    for (int i = 0; i < client.symbol_count; i++) {
        char underlying[16];
        // Extract underlying from option symbol (e.g., "QQQ" from "QQQ251220C00564000")
        int j;
        for (j = 0; j < (int)sizeof(underlying) - 1 && client.symbols[i][j] && 
             ((client.symbols[i][j] >= 'A' && client.symbols[i][j] <= 'Z') || 
              (client.symbols[i][j] >= 'a' && client.symbols[i][j] <= 'z')); j++) {
            underlying[j] = client.symbols[i][j];
        }
        underlying[j] = '\0';
        
        // Check if we already have this underlying
        int already_exists = 0;
        for (int k = 0; k < underlying_count; k++) {
            if (strcmp(underlying_symbols[k], underlying) == 0) {
                already_exists = 1;
                break;
            }
        }
        
        if (!already_exists && underlying_count < MAX_SYMBOLS) {
            strcpy(underlying_symbols[underlying_count], underlying);
            underlying_count++;
        }
    }
    
    // Fetch historical data for each underlying (60 days from recent date) - use config if available
    if (config.valid && underlying_count > 0) {
        printf("Initializing realized volatility analysis...\n");
        for (int i = 0; i < underlying_count; i++) {
            fetch_historical_bars(&client, underlying_symbols[i], "2025-06-01", 60);
        }
        printf("\n");
    }
    
    // Set up signal handler
    signal(SIGINT, sigint_handler);
    
    if (mock_mode) {
        // Mock mode - no WebSocket connection needed, but initialize stock client for underlying prices
        printf("=== Mock Mode (Development) ===\n");
        printf("Risk-free rate: %.4f%% (for theoretical Greeks calculations)\n\n", client.risk_free_rate * 100);
        
        // Initialize stock client for mock underlying price storage (needed for Greeks calculations)
        if (!init_stock_client_for_mock(&client)) {
            printf("Warning: Failed to initialize stock price cache for mock mode\n");
            // Continue anyway - some functionality will be limited
        }
        
        display_symbols_list(&client, "Mock streaming for symbols");
        printf("Press Ctrl+C to exit\n\n");
        
        // Start display thread
        if (!start_display_thread(&client)) {
            printf("Failed to start display thread\n");
            return 1;
        }
        
        // Start mock data stream
        start_mock_data_stream(&client);
        
        // Keep main thread alive
        while (!client.interrupted) {
            sleep(1);
        }
        
        stop_mock_data_stream();
        stop_display_thread(&client);
        
        // Cleanup stock client if it was initialized
        if (client.stock_client) {
            stock_websocket_disconnect(&client);
        }
    } else {
        // Real WebSocket mode (curl already initialized)
        
        // Connect to dual WebSocket (options + stocks)
        if (!dual_websocket_connect(&client)) {
            curl_global_cleanup();
            return 1;
        }
        
        // Display streaming symbols
        display_symbols_list(&client, "Streaming options data for symbols");
        printf("Press Ctrl+C to exit\n\n");
        
        // Start display thread
        if (!start_display_thread(&client)) {
            printf("Failed to start display thread\n");
            dual_websocket_disconnect(&client);
            curl_global_cleanup();
            return 1;
        }
        
        // Main event loop
        while (!client.interrupted && client.wsi) {
            dual_websocket_service(&client, 50);
        }
        
        printf("\nShutting down...\n");
        
        // Stop display thread
        stop_display_thread(&client);
        
        // Cleanup
        dual_websocket_disconnect(&client);
        curl_global_cleanup();
    }
    
    return 0;
}