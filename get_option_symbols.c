#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

struct APIResponse {
    char *data;
    size_t size;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, struct APIResponse *response) {
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

int main(int argc, char *argv[]) {
    if (argc != 6 && argc != 8) {
        printf("Usage: %s <API_KEY> <API_SECRET> <SYMBOL> <EXPIRATION_DATE_GTE> <EXPIRATION_DATE_LTE> [STRIKE_GTE] [STRIKE_LTE]\n", argv[0]);
        printf("Examples:\n");
        printf("  Dates only: %s YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20\n", argv[0]);
        printf("  With strikes: %s YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20 150.00 160.00\n", argv[0]);
        printf("\nNotes:\n");
        printf("  - For single date: use same date for both GTE and LTE\n");
        printf("  - For date range: GTE should be earlier, LTE should be later\n");
        printf("  - Date format: YYYY-MM-DD\n");
        printf("  - Use 0 for STRIKE_GTE or STRIKE_LTE to skip that filter\n");
        return 1;
    }
    
    char *api_key = argv[1];
    char *api_secret = argv[2];
    char *symbol = argv[3];
    char *expiration_date_gte = argv[4];
    char *expiration_date_lte = argv[5];
    double strike_price_gte = 0.0;
    double strike_price_lte = 0.0;
    
    if (argc == 8) {
        strike_price_gte = atof(argv[6]);
        strike_price_lte = atof(argv[7]);
    }
    
    CURL *curl;
    CURLcode res;
    struct APIResponse response = {0};
    
    curl = curl_easy_init();
    if (!curl) {
        printf("Failed to initialize CURL\n");
        return 1;
    }
    
    // Build URL with query parameters
    char url[512];
    int url_len = snprintf(url, sizeof(url), 
                          "https://api.alpaca.markets/v2/options/contracts?underlying_symbols=%s&expiration_date_gte=%s&expiration_date_lte=%s",
                          symbol, expiration_date_gte, expiration_date_lte);
    
    // Add strike price filters if specified
    if (strike_price_gte > 0) {
        url_len += snprintf(url + url_len, sizeof(url) - url_len, "&strike_price_gte=%.2f", strike_price_gte);
    }
    if (strike_price_lte > 0) {
        url_len += snprintf(url + url_len, sizeof(url) - url_len, "&strike_price_lte=%.2f", strike_price_lte);
    }
    
    printf("Fetching option contracts for %s", symbol);
    if (strcmp(expiration_date_gte, expiration_date_lte) == 0) {
        printf(" expiring on %s", expiration_date_gte);
    } else {
        printf(" expiring between %s and %s", expiration_date_gte, expiration_date_lte);
    }
    if (strike_price_gte > 0 || strike_price_lte > 0) {
        printf(" (strike");
        if (strike_price_gte > 0) printf(" >= $%.2f", strike_price_gte);
        if (strike_price_lte > 0) printf(" <= $%.2f", strike_price_lte);
        printf(")");
    }
    printf("...\n");
    printf("URL: %s\n\n", url);
    
    // Set headers
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "APCA-API-KEY-ID: %s", api_key);
    headers = curl_slist_append(headers, auth_header);
    
    char secret_header[256];
    snprintf(secret_header, sizeof(secret_header), "APCA-API-SECRET-KEY: %s", api_secret);
    headers = curl_slist_append(headers, secret_header);
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AlpacaOptionsClient/1.0");
    
    // Perform request
    res = curl_easy_perform(curl);
    
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
                    printf("Found %d option contracts:\n\n", count);
                    
                    printf("%-25s %-6s %-10s %-10s\n", "SYMBOL", "TYPE", "STRIKE", "EXPIRY");
                    printf("%-25s %-6s %-10s %-10s\n", "-------------------------", "------", "----------", "----------");
                    
                    for (int i = 0; i < count; i++) {
                        cJSON *contract = cJSON_GetArrayItem(option_contracts, i);
                        
                        cJSON *symbol_obj = cJSON_GetObjectItem(contract, "symbol");
                        cJSON *type_obj = cJSON_GetObjectItem(contract, "type");
                        cJSON *strike_obj = cJSON_GetObjectItem(contract, "strike_price");
                        cJSON *expiry_obj = cJSON_GetObjectItem(contract, "expiration_date");
                        
                        if (symbol_obj && type_obj && strike_obj && expiry_obj) {
                            printf("%-25s %-6s %-10.2f %-10s\n",
                                   symbol_obj->valuestring,
                                   type_obj->valuestring,
                                   strike_obj->valuedouble,
                                   expiry_obj->valuestring);
                        }
                    }
                    
                    printf("\nTo stream these options, use symbols like:\n");
                    for (int i = 0; i < count && i < 5; i++) {  // Show first 5 as examples
                        cJSON *contract = cJSON_GetArrayItem(option_contracts, i);
                        cJSON *symbol_obj = cJSON_GetObjectItem(contract, "symbol");
                        if (symbol_obj) {
                            printf("./alpaca_options_stream YOUR_KEY YOUR_SECRET %s\n", symbol_obj->valuestring);
                        }
                    }
                    if (count > 5) {
                        printf("... and %d more\n", count - 5);
                    }
                } else {
                    printf("No option contracts found in response\n");
                }
                cJSON_Delete(json);
            } else {
                printf("Failed to parse JSON response\n");
                printf("Raw response: %s\n", response.data);
            }
        } else {
            printf("API request failed with status code: %ld\n", response_code);
            printf("Response: %s\n", response.data);
        }
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (response.data) {
        free(response.data);
    }
    
    return 0;
}