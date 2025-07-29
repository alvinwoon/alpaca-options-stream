#include "../include/api_client.h"
#include "../include/display.h"
#include "../include/realized_vol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

size_t api_response_callback(void *contents, size_t size, size_t nmemb, api_response_t *response) {
    size_t total_size = size * nmemb;
    char *ptr = realloc(response->data, response->size + total_size + 1);
    if (ptr == NULL) {
        printf("Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    
    response->data = ptr;
    memcpy(&(response->data[response->size]), contents, total_size);
    response->size += total_size;
    response->data[response->size] = 0;
    
    return total_size;
}

int fetch_option_symbols(alpaca_client_t *client, const char *underlying_symbol, 
                        const char *exp_date_gte, const char *exp_date_lte, 
                        double strike_price_gte, double strike_price_lte) {
    CURL *curl;
    CURLcode res;
    api_response_t response = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize CURL\n");
        return 0;
    }
    
    // Build URL with query parameters
    char url[512];
    int url_len = snprintf(url, sizeof(url), 
                          "https://api.alpaca.markets/v2/options/contracts?underlying_symbols=%s&expiration_date_gte=%s&expiration_date_lte=%s",
                          underlying_symbol, exp_date_gte, exp_date_lte);
    
    // Add strike price filters if specified
    if (strike_price_gte > 0) {
        url_len += snprintf(url + url_len, sizeof(url) - url_len, "&strike_price_gte=%.2f", strike_price_gte);
    }
    if (strike_price_lte > 0) {
        url_len += snprintf(url + url_len, sizeof(url) - url_len, "&strike_price_lte=%.2f", strike_price_lte);
    }
    
    printf("Fetching option contracts for %s (expiring %s to %s", 
           underlying_symbol, exp_date_gte, exp_date_lte);
    if (strike_price_gte > 0 || strike_price_lte > 0) {
        printf(", strike");
        if (strike_price_gte > 0) printf(" >= $%.2f", strike_price_gte);
        if (strike_price_lte > 0) printf(" <= $%.2f", strike_price_lte);
    }
    printf(")...\n");
    
    // Set headers
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "APCA-API-KEY-ID: %s", client->api_key);
    headers = curl_slist_append(headers, auth_header);
    
    char secret_header[256];
    snprintf(secret_header, sizeof(secret_header), "APCA-API-SECRET-KEY: %s", client->api_secret);
    headers = curl_slist_append(headers, secret_header);
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AlpacaOptionsClient/1.0");
    
    // Perform request
    res = curl_easy_perform(curl);
    
    int success = 0;
    if (res != CURLE_OK) {
        printf("CURL request failed: %s\n", curl_easy_strerror(res));
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        if (response_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response.data);
            if (json) {
                cJSON *option_contracts = cJSON_GetObjectItem(json, "option_contracts");
                if (cJSON_IsArray(option_contracts)) {
                    int count = cJSON_GetArraySize(option_contracts);
                    printf("Found %d option contracts\n", count);
                    
                    client->symbol_count = 0;
                    for (int i = 0; i < count && client->symbol_count < MAX_SYMBOLS; i++) {
                        cJSON *contract = cJSON_GetArrayItem(option_contracts, i);
                        cJSON *symbol_obj = cJSON_GetObjectItem(contract, "symbol");
                        
                        if (symbol_obj && cJSON_IsString(symbol_obj)) {
                            strncpy(client->symbols[client->symbol_count], symbol_obj->valuestring, 
                                   sizeof(client->symbols[client->symbol_count]) - 1);
                            client->symbols[client->symbol_count][sizeof(client->symbols[client->symbol_count]) - 1] = '\0';
                            client->symbol_count++;
                        }
                    }
                    
                    display_symbols_list(client, "Selected symbols for streaming");
                    success = 1;
                } else {
                    printf("No option contracts found in response\n");
                }
                cJSON_Delete(json);
            } else {
                printf("Failed to parse JSON response\n");
            }
        } else {
            printf("API request failed with status code: %ld\n", response_code);
            if (response.data) {
                printf("Response: %s\n", response.data);
            }
        }
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.data) {
        free(response.data);
    }
    
    return success;
}

// Fetch historical OHLC data for RV calculation
int fetch_historical_bars(alpaca_client_t *client, const char *symbol, const char *start_date, int limit_days) {
    CURL *curl;
    CURLcode res;
    api_response_t response = {0};
    int success = 0;
    
    if (!client || !symbol || !start_date) {
        printf("Invalid parameters for historical data fetch\n");
        return 0;
    }
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize CURL for historical data\n");
        return 0;
    }
    
    // Build URL for historical bars API (use data.alpaca.markets with IEX feed for free tier)
    char url[512];
    snprintf(url, sizeof(url), 
             "https://data.alpaca.markets/v2/stocks/%s/bars?timeframe=1Day&start=%s&limit=%d&feed=iex",
             symbol, start_date, limit_days);
    
    printf("Fetching historical data: %s (last %d days)\n", symbol, limit_days);
    printf("Full API URL: %s\n", url);
    
    // Set CURL options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "alpaca-options-stream/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    // Set headers with API key
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "APCA-API-KEY-ID: %s", client->api_key);
    headers = curl_slist_append(headers, auth_header);
    
    char secret_header[256];
    snprintf(secret_header, sizeof(secret_header), "APCA-API-SECRET-KEY: %s", client->api_secret);
    headers = curl_slist_append(headers, secret_header);
    
    printf("Auth Headers:\n");
    printf("   APCA-API-KEY-ID: %.*s...\n", 8, client->api_key);
    printf("   APCA-API-SECRET-KEY: %.*s...\n", 8, client->api_secret);
    
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform the request
    res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        printf("CURL request failed: %s\n", curl_easy_strerror(res));
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        if (response_code == 200 && response.data) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response.data);
            if (json) {
                cJSON *bars = cJSON_GetObjectItem(json, "bars");
                if (cJSON_IsArray(bars)) {
                    int bar_count = cJSON_GetArraySize(bars);
                    printf("   Retrieved %d historical bars for %s\n", bar_count, symbol);
                    
                    // Initialize RV manager if not already done
                    if (!client->rv_manager) {
                        client->rv_manager = init_rv_manager();
                    }
                    
                    if (client->rv_manager) {
                        realized_vol_t *rv = get_underlying_rv(client->rv_manager, symbol);
                        if (rv) {
                            // Process each bar and add to RV data
                            for (int i = 0; i < bar_count; i++) {
                                cJSON *bar = cJSON_GetArrayItem(bars, i);
                                if (bar) {
                                    cJSON *open = cJSON_GetObjectItem(bar, "o");
                                    cJSON *high = cJSON_GetObjectItem(bar, "h");
                                    cJSON *low = cJSON_GetObjectItem(bar, "l");
                                    cJSON *close = cJSON_GetObjectItem(bar, "c");
                                    
                                    if (cJSON_IsNumber(open) && cJSON_IsNumber(high) && 
                                        cJSON_IsNumber(low) && cJSON_IsNumber(close)) {
                                        
                                        update_price_data(rv, 
                                                        cJSON_GetNumberValue(open),
                                                        cJSON_GetNumberValue(high),
                                                        cJSON_GetNumberValue(low),
                                                        cJSON_GetNumberValue(close));
                                    }
                                }
                            }
                            
                            // Display RV summary
                            if (rv->rv_20d > 0) {
                                printf("   RV Analysis: 10d=%.1f%% 20d=%.1f%% 30d=%.1f%% (trend: %+.1f%%)\n",
                                       rv->rv_10d * 100, rv->rv_20d * 100, rv->rv_30d * 100, rv->rv_trend * 100);
                            }
                            
                            success = 1;
                        }
                    }
                }
                cJSON_Delete(json);
            } else {
                printf("Failed to parse historical data JSON response\n");
            }
        } else {
            printf("Historical data request failed with status code: %ld\n", response_code);
            if (response.data) {
                printf("Response: %s\n", response.data);
            }
        }
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.data) {
        free(response.data);
    }
    
    return success;
}