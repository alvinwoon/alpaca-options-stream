#include "../include/fred_api.h"
#include "../include/api_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <time.h>

const char* select_treasury_series(double time_to_expiry) {
    // Select appropriate Treasury rate based on option expiry
    if (time_to_expiry <= 0.25) {
        // 3 months or less - use 3-month Treasury
        return FRED_3_MONTH_TREASURY;
    } else if (time_to_expiry <= 2.0) {
        // Up to 2 years - use Federal Funds rate
        return FRED_FEDERAL_FUNDS;
    } else {
        // Longer term - use 10-year Treasury
        return FRED_10_YEAR_TREASURY;
    }
}

double get_risk_free_rate_for_expiry(double time_to_expiry, const char *api_key) {
    double rate;
    const char *series_id = select_treasury_series(time_to_expiry);
    
    if (fetch_fred_rate(series_id, &rate, api_key)) {
        return rate / 100.0; // Convert percentage to decimal
    }
    
    return DEFAULT_RISK_FREE_RATE; // Fallback rate
}

int fetch_fred_rate(const char *series_id, double *rate, const char *api_key) {
    if (!series_id || !rate) return 0;
    
    // Require user to provide their own FRED API key
    if (!api_key || strlen(api_key) == 0) {
        printf("⚠️  No FRED API key provided. Get a free key at: https://fred.stlouisfed.org/docs/api/api_key.html\n");
        printf("   Add 'fred_api_key' to your config.json file for live risk-free rates.\n");
        printf("   Using default risk-free rate: %.2f%%\n", DEFAULT_RISK_FREE_RATE * 100);
        return 0;
    }
    
    CURL *curl;
    CURLcode res;
    api_response_t response = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize CURL for FRED API\n");
        return 0;
    }
    
    // Get current date for latest observation
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char current_date[11];
    strftime(current_date, sizeof(current_date), "%Y-%m-%d", tm_info);
    
    // Build FRED API URL
    // Format: https://api.stlouisfed.org/fred/series/observations?series_id=DGS3MO&api_key=KEY&file_type=json&limit=1&sort_order=desc
    char url[512];
    snprintf(url, sizeof(url), 
             "%s?series_id=%s&api_key=%s&file_type=json&limit=1&sort_order=desc",
             FRED_BASE_URL, series_id, api_key);
    
    printf("📊 Fetching risk-free rate from FRED API (series: %s)...\n", series_id);
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AlpacaOptionsClient/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // 10 second timeout
    
    // Perform request
    res = curl_easy_perform(curl);
    
    int success = 0;
    if (res != CURLE_OK) {
        printf("FRED API request failed: %s\n", curl_easy_strerror(res));
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        
        if (response_code == 200) {
            // Parse JSON response
            cJSON *json = cJSON_Parse(response.data);
            if (json) {
                cJSON *observations = cJSON_GetObjectItem(json, "observations");
                if (cJSON_IsArray(observations) && cJSON_GetArraySize(observations) > 0) {
                    cJSON *latest = cJSON_GetArrayItem(observations, 0);
                    if (latest) {
                        cJSON *value = cJSON_GetObjectItem(latest, "value");
                        cJSON *date = cJSON_GetObjectItem(latest, "date");
                        
                        if (value && cJSON_IsString(value)) {
                            // Check if value is "." (missing data)
                            if (strcmp(value->valuestring, ".") == 0) {
                                printf("FRED data not available for series %s\n", series_id);
                            } else {
                                *rate = atof(value->valuestring);
                                printf("FRED rate (%s): %.4f%% (date: %s)\n", 
                                       series_id, *rate, 
                                       (date && cJSON_IsString(date)) ? date->valuestring : "unknown");
                                success = 1;
                            }
                        }
                    }
                } else {
                    printf("No observations found in FRED response\n");
                }
                cJSON_Delete(json);
            } else {
                printf("Failed to parse FRED JSON response\n");
                if (response.data) {
                    printf("Raw response: %.200s\n", response.data); // First 200 chars
                }
            }
        } else {
            printf("FRED API request failed with status code: %ld\n", response_code);
            if (response.data) {
                printf("Response: %.200s\n", response.data);
            }
        }
    }
    
    // Cleanup
    curl_easy_cleanup(curl);
    if (response.data) {
        free(response.data);
    }
    
    return success;
}

int fetch_risk_free_rate(double *rate, const char *api_key) {
    if (!rate) return 0;
    
    // Try 3-month Treasury first (most commonly used for options)
    if (fetch_fred_rate(FRED_3_MONTH_TREASURY, rate, api_key)) {
        return 1;
    }
    
    printf("3-month Treasury rate unavailable, trying Federal Funds rate...\n");
    
    // Try Federal Funds rate as backup
    if (fetch_fred_rate(FRED_FEDERAL_FUNDS, rate, api_key)) {
        return 1;
    }
    
    printf("Federal Funds rate unavailable, trying 10-year Treasury...\n");
    
    // Try 10-year Treasury as last resort
    if (fetch_fred_rate(FRED_10_YEAR_TREASURY, rate, api_key)) {
        return 1;
    }
    
    printf("All FRED rates unavailable, using default rate: %.2f%%\n", DEFAULT_RISK_FREE_RATE * 100);
    *rate = DEFAULT_RISK_FREE_RATE * 100; // Return as percentage for consistency
    
    return 1; // Always return success with fallback
}