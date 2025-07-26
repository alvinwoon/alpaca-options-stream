#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <libwebsockets.h>
#include <msgpack.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define MAX_PAYLOAD 4096
#define MAX_SYMBOLS 100

typedef struct {
    char symbol[64];
    // Quote data
    double bid_price;
    int bid_size;
    char bid_exchange[8];
    double ask_price;
    int ask_size;
    char ask_exchange[8];
    char quote_time[32];
    char quote_condition[8];
    int has_quote;
    // Trade data
    double last_price;
    int last_size;
    char trade_exchange[8];
    char trade_time[32];
    char trade_condition[8];
    int has_trade;
} option_data_t;

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

typedef struct {
    char *api_key;
    char *api_secret;
    struct lws_context *context;
    struct lws *wsi;
    int authenticated;
    int subscribed;
    int interrupted;
    char symbols[MAX_SYMBOLS][32];
    int symbol_count;
    option_data_t option_data[MAX_SYMBOLS];
    int data_count;
} alpaca_client_t;

static alpaca_client_t client = {0};

static void sigint_handler(int sig) {
    client.interrupted = 1;
}

static void parse_option_symbol(const char *symbol, char *readable, size_t readable_size) {
    if (!symbol || !readable || readable_size < 64) {
        if (readable && readable_size > 0) {
            strncpy(readable, symbol ? symbol : "N/A", readable_size - 1);
            readable[readable_size - 1] = '\0';
        }
        return;
    }
    
    // Expected format: SYMBOL[YY][MM][DD][C/P][00000000]
    // Example: QQQ250801C00560000
    
    size_t len = strlen(symbol);
    if (len < 15) {  // Minimum length for a valid option symbol
        strncpy(readable, symbol, readable_size - 1);
        readable[readable_size - 1] = '\0';
        return;
    }
    
    // Find where the date starts (after the underlying symbol)
    // Look for the pattern: 6 digits followed by C/P followed by 8 digits
    const char *date_start = NULL;
    for (size_t i = 1; i <= len - 15; i++) {
        if (i + 14 < len &&
            isdigit(symbol[i]) && isdigit(symbol[i+1]) &&     // YY
            isdigit(symbol[i+2]) && isdigit(symbol[i+3]) &&   // MM  
            isdigit(symbol[i+4]) && isdigit(symbol[i+5]) &&   // DD
            (symbol[i+6] == 'C' || symbol[i+6] == 'P') &&     // Type
            isdigit(symbol[i+7])) {                           // Strike start
            date_start = symbol + i;
            break;
        }
    }
    
    if (!date_start) {
        strncpy(readable, symbol, readable_size - 1);
        readable[readable_size - 1] = '\0';
        return;
    }
    
    // Extract components
    size_t underlying_len = date_start - symbol;
    char underlying[32];
    strncpy(underlying, symbol, underlying_len);
    underlying[underlying_len] = '\0';
    
    // Extract date components (YYMMDD)
    char year_str[3] = {date_start[0], date_start[1], '\0'};
    char month_str[3] = {date_start[2], date_start[3], '\0'};
    char day_str[3] = {date_start[4], date_start[5], '\0'};
    
    // Extract option type
    char option_type = date_start[6];
    
    // Extract strike price (8 digits, divide by 1000 to get actual price)
    char strike_str[9];
    strncpy(strike_str, date_start + 7, 8);
    strike_str[8] = '\0';
    double strike_price = atoi(strike_str) / 1000.0;
    
    // Format the readable string
    snprintf(readable, readable_size, "%s %s/%s/%s $%.2f %s",
             underlying,
             month_str, day_str, year_str,
             strike_price,
             option_type == 'C' ? "Call" : "Put");
}

static option_data_t* find_or_create_option_data(const char *symbol) {
    // First try to find existing entry
    for (int i = 0; i < client.data_count; i++) {
        if (strcmp(client.option_data[i].symbol, symbol) == 0) {
            return &client.option_data[i];
        }
    }
    
    // Create new entry if we have space
    if (client.data_count < MAX_SYMBOLS) {
        option_data_t *new_data = &client.option_data[client.data_count];
        memset(new_data, 0, sizeof(option_data_t));
        strncpy(new_data->symbol, symbol, sizeof(new_data->symbol) - 1);
        client.data_count++;
        return new_data;
    }
    
    return NULL;
}

static void display_option_data() {
    // Clear screen and move cursor to top
    printf("\033[2J\033[H");
    
    printf("=== Alpaca Options Live Data ===\n");
    printf("Symbols: %d | Press Ctrl+C to exit\n\n", client.data_count);
    
    printf("%-35s %-12s %-15s %-15s %-12s %-15s\n", 
           "OPTION CONTRACT", "LAST TRADE", "BID", "ASK", "SPREAD", "LAST UPDATE");
    printf("%-35s %-12s %-15s %-15s %-12s %-15s\n", 
           "-----------------------------------", "------------", "---------------", "---------------", "------------", "---------------");
    
    for (int i = 0; i < client.data_count; i++) {
        option_data_t *data = &client.option_data[i];
        
        // Calculate spread
        double spread = (data->has_quote && data->ask_price > 0 && data->bid_price > 0) ? 
                       (data->ask_price - data->bid_price) : 0.0;
        
        // Format last trade
        char trade_str[32] = "N/A";
        if (data->has_trade) {
            snprintf(trade_str, sizeof(trade_str), "$%.4f x%d", data->last_price, data->last_size);
        }
        
        // Format bid
        char bid_str[32] = "N/A";
        if (data->has_quote && data->bid_price > 0) {
            snprintf(bid_str, sizeof(bid_str), "$%.4f x%d", data->bid_price, data->bid_size);
        }
        
        // Format ask
        char ask_str[32] = "N/A";
        if (data->has_quote && data->ask_price > 0) {
            snprintf(ask_str, sizeof(ask_str), "$%.4f x%d", data->ask_price, data->ask_size);
        }
        
        // Format spread
        char spread_str[16] = "N/A";
        if (spread > 0) {
            snprintf(spread_str, sizeof(spread_str), "$%.4f", spread);
        }
        
        // Use the most recent timestamp
        char last_time_str[32] = "N/A";
        if (data->has_quote && strlen(data->quote_time) > 0) {
            const char *time_part = data->quote_time + 11; // Skip date part, show time only
            strncpy(last_time_str, time_part, sizeof(last_time_str) - 1);
            last_time_str[sizeof(last_time_str) - 1] = '\0';
            if (strlen(last_time_str) > 12) last_time_str[12] = '\0'; // Truncate nanoseconds
        } else if (data->has_trade && strlen(data->trade_time) > 0) {
            const char *time_part = data->trade_time + 11; // Skip date part, show time only
            strncpy(last_time_str, time_part, sizeof(last_time_str) - 1);
            last_time_str[sizeof(last_time_str) - 1] = '\0';
            if (strlen(last_time_str) > 12) last_time_str[12] = '\0'; // Truncate nanoseconds
        }
        
        // Format symbol in readable format
        char readable_symbol[64];
        parse_option_symbol(data->symbol, readable_symbol, sizeof(readable_symbol));
        
        printf("%-35s %-12s %-15s %-15s %-12s %-15s\n",
               readable_symbol, trade_str, bid_str, ask_str, spread_str, last_time_str);
    }
    
    printf("\nLive streaming... (data updates in real-time)\n");
    fflush(stdout);
}

static int fetch_option_symbols(const char *underlying_symbol, const char *exp_date_gte, const char *exp_date_lte, 
                                double strike_price_gte, double strike_price_lte) {
    CURL *curl;
    CURLcode res;
    struct APIResponse response = {0};
    
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
    snprintf(auth_header, sizeof(auth_header), "APCA-API-KEY-ID: %s", client.api_key);
    headers = curl_slist_append(headers, auth_header);
    
    char secret_header[256];
    snprintf(secret_header, sizeof(secret_header), "APCA-API-SECRET-KEY: %s", client.api_secret);
    headers = curl_slist_append(headers, secret_header);
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
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
                    
                    client.symbol_count = 0;
                    for (int i = 0; i < count && client.symbol_count < MAX_SYMBOLS; i++) {
                        cJSON *contract = cJSON_GetArrayItem(option_contracts, i);
                        cJSON *symbol_obj = cJSON_GetObjectItem(contract, "symbol");
                        
                        if (symbol_obj && cJSON_IsString(symbol_obj)) {
                            strncpy(client.symbols[client.symbol_count], symbol_obj->valuestring, 
                                   sizeof(client.symbols[client.symbol_count]) - 1);
                            client.symbols[client.symbol_count][sizeof(client.symbols[client.symbol_count]) - 1] = '\0';
                            client.symbol_count++;
                        }
                    }
                    
                    printf("Selected %d symbols for streaming:\n", client.symbol_count);
                    for (int i = 0; i < client.symbol_count; i++) {
                        char readable_symbol[64];
                        parse_option_symbol(client.symbols[i], readable_symbol, sizeof(readable_symbol));
                        printf("  %s (%s)\n", readable_symbol, client.symbols[i]);
                    }
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

static void send_auth_message(struct lws *wsi) {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    
    // Pack authentication message as map with 3 elements
    msgpack_pack_map(&pk, 3);
    
    // "action": "auth"
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "action", 6);
    msgpack_pack_str(&pk, 4);
    msgpack_pack_str_body(&pk, "auth", 4);
    
    // "key": api_key
    msgpack_pack_str(&pk, 3);
    msgpack_pack_str_body(&pk, "key", 3);
    size_t key_len = strlen(client.api_key);
    msgpack_pack_str(&pk, key_len);
    msgpack_pack_str_body(&pk, client.api_key, key_len);
    
    // "secret": api_secret
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "secret", 6);
    size_t secret_len = strlen(client.api_secret);
    msgpack_pack_str(&pk, secret_len);
    msgpack_pack_str_body(&pk, client.api_secret, secret_len);
    
    unsigned char buf[LWS_PRE + sbuf.size];
    memcpy(&buf[LWS_PRE], sbuf.data, sbuf.size);
    
    lws_write(wsi, &buf[LWS_PRE], sbuf.size, LWS_WRITE_BINARY);
    
    printf("Sent authentication message (MsgPack)");
    
    msgpack_sbuffer_destroy(&sbuf);
}

static void send_subscription_message(struct lws *wsi) {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    
    // Pack subscription message as map with 3 elements (trades and quotes)
    msgpack_pack_map(&pk, 3);
    
    // "action": "subscribe"
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "action", 6);
    msgpack_pack_str(&pk, 9);
    msgpack_pack_str_body(&pk, "subscribe", 9);
    
    // "trades": [symbols...]
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "trades", 6);
    msgpack_pack_array(&pk, client.symbol_count);
    for (int i = 0; i < client.symbol_count; i++) {
        size_t sym_len = strlen(client.symbols[i]);
        msgpack_pack_str(&pk, sym_len);
        msgpack_pack_str_body(&pk, client.symbols[i], sym_len);
    }
    
    // "quotes": [symbols...]
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "quotes", 6);
    msgpack_pack_array(&pk, client.symbol_count);
    for (int i = 0; i < client.symbol_count; i++) {
        size_t sym_len = strlen(client.symbols[i]);
        msgpack_pack_str(&pk, sym_len);
        msgpack_pack_str_body(&pk, client.symbols[i], sym_len);
    }
    
    unsigned char buf[LWS_PRE + sbuf.size];
    memcpy(&buf[LWS_PRE], sbuf.data, sbuf.size);
    
    lws_write(wsi, &buf[LWS_PRE], sbuf.size, LWS_WRITE_BINARY);
    
    printf("Sent subscription message for %d symbols - trades & quotes (MsgPack, %lu bytes)", client.symbol_count, sbuf.size);
    
    // Debug: print hex dump of the message
   // printf("MsgPack hex dump: ");
    //for (size_t i = 0; i < sbuf.size; i++) {
      //  printf("%02x ", (unsigned char)sbuf.data[i]);
        //if (i > 0 && (i + 1) % 16 == 0) printf("\n                  ");
    //}
    printf("\n");
    printf("Subscribed symbols:\n");
    for (int i = 0; i < client.symbol_count; i++) {
        char readable_symbol[64];
        parse_option_symbol(client.symbols[i], readable_symbol, sizeof(readable_symbol));
        printf("  - %s (%s)\n", readable_symbol, client.symbols[i]);
    }
    
    msgpack_sbuffer_destroy(&sbuf);
}

static const char* extract_string_from_msgpack(msgpack_object *obj) {
    if (obj->type == MSGPACK_OBJECT_STR) {
        static char buffer[256];
        size_t len = obj->via.str.size;
        if (len >= sizeof(buffer)) len = sizeof(buffer) - 1;
        memcpy(buffer, obj->via.str.ptr, len);
        buffer[len] = '\0';
        return buffer;
    }
    return NULL;
}

static void parse_option_trade(msgpack_object *trade_obj) {
    if (trade_obj->type != MSGPACK_OBJECT_MAP) return;
    
    char symbol[64] = {0};
    char timestamp_str[64] = {0};
    double price = 0.0;
    int size = 0;
    char exchange[8] = {0};
    char condition[8] = {0};
    
    msgpack_object_map *map = &trade_obj->via.map;
    for (uint32_t i = 0; i < map->size; i++) {
        msgpack_object *key = &map->ptr[i].key;
        msgpack_object *val = &map->ptr[i].val;
        
        if (key->type == MSGPACK_OBJECT_STR) {
            if (strncmp(key->via.str.ptr, "S", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(symbol)) len = sizeof(symbol) - 1;
                    memcpy(symbol, val->via.str.ptr, len);
                    symbol[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "t", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(timestamp_str)) len = sizeof(timestamp_str) - 1;
                    memcpy(timestamp_str, val->via.str.ptr, len);
                    timestamp_str[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "p", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_FLOAT64) price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_FLOAT32) price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) price = val->via.u64;
            } else if (strncmp(key->via.str.ptr, "s", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) size = val->via.u64;
                else if (val->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) size = val->via.i64;
            } else if (strncmp(key->via.str.ptr, "x", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(exchange)) len = sizeof(exchange) - 1;
                    memcpy(exchange, val->via.str.ptr, len);
                    exchange[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "c", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(condition)) len = sizeof(condition) - 1;
                    memcpy(condition, val->via.str.ptr, len);
                    condition[len] = '\0';
                }
            }
        }
    }
    
    if (strlen(symbol) > 0) {
        option_data_t *data = find_or_create_option_data(symbol);
        if (data) {
            data->last_price = price;
            data->last_size = size;
            strncpy(data->trade_exchange, exchange, sizeof(data->trade_exchange) - 1);
            strncpy(data->trade_time, timestamp_str, sizeof(data->trade_time) - 1);
            strncpy(data->trade_condition, condition, sizeof(data->trade_condition) - 1);
            data->has_trade = 1;
            
            display_option_data();
        }
    }
}

static void parse_option_quote(msgpack_object *quote_obj) {
    if (quote_obj->type != MSGPACK_OBJECT_MAP) return;
    
    char symbol[64] = {0};
    char timestamp_str[64] = {0};
    char bid_exchange[8] = {0};
    double bid_price = 0.0;
    int bid_size = 0;
    char ask_exchange[8] = {0};
    double ask_price = 0.0;
    int ask_size = 0;
    char condition[8] = {0};
    
    msgpack_object_map *map = &quote_obj->via.map;
    for (uint32_t i = 0; i < map->size; i++) {
        msgpack_object *key = &map->ptr[i].key;
        msgpack_object *val = &map->ptr[i].val;
        
        if (key->type == MSGPACK_OBJECT_STR) {
            if (strncmp(key->via.str.ptr, "S", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(symbol)) len = sizeof(symbol) - 1;
                    memcpy(symbol, val->via.str.ptr, len);
                    symbol[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "t", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(timestamp_str)) len = sizeof(timestamp_str) - 1;
                    memcpy(timestamp_str, val->via.str.ptr, len);
                    timestamp_str[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "bx", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(bid_exchange)) len = sizeof(bid_exchange) - 1;
                    memcpy(bid_exchange, val->via.str.ptr, len);
                    bid_exchange[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "bp", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_FLOAT64) bid_price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_FLOAT32) bid_price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) bid_price = val->via.u64;
            } else if (strncmp(key->via.str.ptr, "bs", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) bid_size = val->via.u64;
            } else if (strncmp(key->via.str.ptr, "ax", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(ask_exchange)) len = sizeof(ask_exchange) - 1;
                    memcpy(ask_exchange, val->via.str.ptr, len);
                    ask_exchange[len] = '\0';
                }
            } else if (strncmp(key->via.str.ptr, "ap", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_FLOAT64) ask_price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_FLOAT32) ask_price = val->via.f64;
                else if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) ask_price = val->via.u64;
            } else if (strncmp(key->via.str.ptr, "as", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) ask_size = val->via.u64;
            } else if (strncmp(key->via.str.ptr, "c", key->via.str.size) == 0) {
                if (val->type == MSGPACK_OBJECT_STR) {
                    size_t len = val->via.str.size;
                    if (len >= sizeof(condition)) len = sizeof(condition) - 1;
                    memcpy(condition, val->via.str.ptr, len);
                    condition[len] = '\0';
                }
            }
        }
    }
    
    if (strlen(symbol) > 0) {
        option_data_t *data = find_or_create_option_data(symbol);
        if (data) {
            data->bid_price = bid_price;
            data->bid_size = bid_size;
            strncpy(data->bid_exchange, bid_exchange, sizeof(data->bid_exchange) - 1);
            data->ask_price = ask_price;
            data->ask_size = ask_size;
            strncpy(data->ask_exchange, ask_exchange, sizeof(data->ask_exchange) - 1);
            strncpy(data->quote_time, timestamp_str, sizeof(data->quote_time) - 1);
            strncpy(data->quote_condition, condition, sizeof(data->quote_condition) - 1);
            data->has_quote = 1;
            
            display_option_data();
        }
    }
}

static void process_message(const char *data, size_t len) {
    // Debug: print raw message received
    //printf("Received raw message (%zu bytes): ", len);
    //for (size_t i = 0; i < len && i < 100; i++) {  // Limit to first 100 bytes
      //  printf("%02x ", (unsigned char)data[i]);
    //}
    if (len > 100) printf("...");
    printf("\n");
    
    msgpack_zone mempool;
    msgpack_object deserialized;
    
    msgpack_zone_init(&mempool, 2048);
    
    msgpack_unpack_return ret = msgpack_unpack(data, len, NULL, &mempool, &deserialized);
    if (ret != MSGPACK_UNPACK_SUCCESS) {
        printf("Failed to parse MsgPack message (return code: %d)\n", ret);
        msgpack_zone_destroy(&mempool);
        return;
    }
    
    if (deserialized.type == MSGPACK_OBJECT_ARRAY) {
        msgpack_object_array *array = &deserialized.via.array;
        for (uint32_t i = 0; i < array->size; i++) {
            msgpack_object *item = &array->ptr[i];
            
            if (item->type == MSGPACK_OBJECT_MAP) {
                msgpack_object_map *map = &item->via.map;
                const char *msg_type = NULL;
                
                // Find the message type "T"
                for (uint32_t j = 0; j < map->size; j++) {
                    msgpack_object *key = &map->ptr[j].key;
                    msgpack_object *val = &map->ptr[j].val;
                    
                    if (key->type == MSGPACK_OBJECT_STR && 
                        key->via.str.size == 1 && 
                        strncmp(key->via.str.ptr, "T", 1) == 0) {
                        msg_type = extract_string_from_msgpack(val);
                        break;
                    }
                }
                
                if (msg_type) {
                    //printf("Received message type: '%s'\n", msg_type);
                    if (strcmp(msg_type, "success") == 0) {
                        printf("Success: authenticated\n");
                        client.authenticated = 1;
                        if (!client.subscribed) {
                            send_subscription_message(client.wsi);
                            client.subscribed = 1;
                        }
                    } else if (strcmp(msg_type, "error") == 0) {
                        printf("Error received from server\n");
                        // Try to extract error message details
                        for (uint32_t k = 0; k < map->size; k++) {
                            msgpack_object *err_key = &map->ptr[k].key;
                            msgpack_object *err_val = &map->ptr[k].val;
                            
                            if (err_key->type == MSGPACK_OBJECT_STR) {
                                printf("  %.*s: ", (int)err_key->via.str.size, err_key->via.str.ptr);
                                if (err_val->type == MSGPACK_OBJECT_STR) {
                                    printf("%.*s\n", (int)err_val->via.str.size, err_val->via.str.ptr);
                                } else if (err_val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                                    printf("%llu", (unsigned long long)err_val->via.u64);
                                    if (err_val->via.u64 == 400) {
                                        printf(" (Bad Request - likely subscription format issue)");
                                    }
                                    printf("\n");
                                } else if (err_val->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                                    printf("%lld\n", (long long)err_val->via.i64);
                                } else {
                                    printf("(unknown type)\n");
                                }
                            }
                        }
                    } else if (strcmp(msg_type, "t") == 0) {
                        parse_option_trade(item);
                    } else if (strcmp(msg_type, "q") == 0) {
                        parse_option_quote(item);
                    } else if (strcmp(msg_type, "subscription") == 0) {
                        printf("Subscription confirmed\n");
                    }
                }
            }
        }
    } else if (deserialized.type == MSGPACK_OBJECT_MAP) {
        // Single message
        const char *msg_type = NULL;
        msgpack_object_map *map = &deserialized.via.map;
        
        for (uint32_t j = 0; j < map->size; j++) {
            msgpack_object *key = &map->ptr[j].key;
            msgpack_object *val = &map->ptr[j].val;
            
            if (key->type == MSGPACK_OBJECT_STR && 
                key->via.str.size == 1 && 
                strncmp(key->via.str.ptr, "T", 1) == 0) {
                msg_type = extract_string_from_msgpack(val);
                break;
            }
        }
        
        if (msg_type) {
            printf("Received single message type: '%s'\n", msg_type);
            if (strcmp(msg_type, "success") == 0) {
                printf("Success: authenticated\n");
                client.authenticated = 1;
                if (!client.subscribed) {
                    send_subscription_message(client.wsi);
                    client.subscribed = 1;
                }
            } else if (strcmp(msg_type, "error") == 0) {
                printf("Error received from server\n");
                // Try to extract error message details
                for (uint32_t k = 0; k < map->size; k++) {
                    msgpack_object *err_key = &map->ptr[k].key;
                    msgpack_object *err_val = &map->ptr[k].val;
                    
                    if (err_key->type == MSGPACK_OBJECT_STR) {
                        printf("  %.*s: ", (int)err_key->via.str.size, err_key->via.str.ptr);
                        if (err_val->type == MSGPACK_OBJECT_STR) {
                            printf("%.*s\n", (int)err_val->via.str.size, err_val->via.str.ptr);
                        } else if (err_val->type == MSGPACK_OBJECT_POSITIVE_INTEGER) {
                            printf("%llu", (unsigned long long)err_val->via.u64);
                            if (err_val->via.u64 == 400) {
                                printf(" (Bad Request - likely subscription format issue)");
                            }
                            printf("\n");
                        } else if (err_val->type == MSGPACK_OBJECT_NEGATIVE_INTEGER) {
                            printf("%lld\n", (long long)err_val->via.i64);
                        } else {
                            printf("(unknown type)\n");
                        }
                    }
                }
            } else if (strcmp(msg_type, "t") == 0) {
                parse_option_trade(&deserialized);
            } else if (strcmp(msg_type, "q") == 0) {
                parse_option_quote(&deserialized);
            }
        }
    }
    
    msgpack_zone_destroy(&mempool);
}

static int callback_alpaca(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("Connected to Alpaca WebSocket\n");
            send_auth_message(wsi);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            {
                process_message((const char*)in, len);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection error\n");
            client.wsi = NULL;
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Connection closed\n");
            client.wsi = NULL;
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            break;
            
        default:
            break;
    }
    
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        "alpaca-options-protocol",
        callback_alpaca,
        0,
        MAX_PAYLOAD,
    },
    { NULL, NULL, 0, 0 }
};

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <API_KEY> <API_SECRET> [ARGS...]\n", argv[0]);
        printf("\nModes:\n");
        printf("1. Direct symbols: %s YOUR_KEY YOUR_SECRET SYMBOL1 SYMBOL2 ...\n", argv[0]);
        printf("   Example: %s YOUR_KEY YOUR_SECRET AAPL241220C00150000 AAPL241220P00150000\n", argv[0]);
        printf("\n2. Auto-fetch mode (dates only): %s YOUR_KEY YOUR_SECRET UNDERLYING EXP_DATE_GTE EXP_DATE_LTE\n", argv[0]);
        printf("   Example: %s YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20\n", argv[0]);
        printf("\n3. Auto-fetch mode (dates + strikes): %s YOUR_KEY YOUR_SECRET UNDERLYING EXP_DATE_GTE EXP_DATE_LTE STRIKE_GTE STRIKE_LTE\n", argv[0]);
        printf("   Example: %s YOUR_KEY YOUR_SECRET AAPL 2024-12-20 2024-12-20 150.00 160.00\n", argv[0]);
        printf("\nNote: Use 0 for STRIKE_GTE or STRIKE_LTE to skip that filter\n");
        return 1;
    }
    
    client.api_key = argv[1];
    client.api_secret = argv[2];
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (argc == 6 || argc == 8) {
        // Auto-fetch mode: dates only (6 args) or dates + strikes (8 args)
        char *underlying = argv[3];
        char *exp_date_gte = argv[4];
        char *exp_date_lte = argv[5];
        double strike_price_gte = 0.0;
        double strike_price_lte = 0.0;
        
        // Parse strike prices if provided
        if (argc == 8) {
            strike_price_gte = atof(argv[6]);
            strike_price_lte = atof(argv[7]);
        }
        
        // Check if dates are in YYYY-MM-DD format (basic validation)
        if (strlen(exp_date_gte) == 10 && exp_date_gte[4] == '-' && exp_date_gte[7] == '-' &&
            strlen(exp_date_lte) == 10 && exp_date_lte[4] == '-' && exp_date_lte[7] == '-') {
            
            printf("=== Auto-fetching option symbols ===\n");
            if (!fetch_option_symbols(underlying, exp_date_gte, exp_date_lte, strike_price_gte, strike_price_lte)) {
                printf("Failed to fetch option symbols\n");
                curl_global_cleanup();
                return 1;
            }
            
            if (client.symbol_count == 0) {
                printf("No option symbols found for the specified criteria\n");
                curl_global_cleanup();
                return 1;
            }
            
            printf("\n=== Starting WebSocket stream ===\n");
        } else {
            // Treat as direct symbols mode
            client.symbol_count = argc - 3;
            if (client.symbol_count > MAX_SYMBOLS) {
                client.symbol_count = MAX_SYMBOLS;
            }
            for (int i = 0; i < client.symbol_count; i++) {
                strncpy(client.symbols[i], argv[i + 3], sizeof(client.symbols[i]) - 1);
                client.symbols[i][sizeof(client.symbols[i]) - 1] = '\0';
            }
            printf("Direct symbols mode: streaming %d symbols\n", client.symbol_count);
        }
    } else if (argc > 3) {
        // Direct symbols mode
        client.symbol_count = argc - 3;
        if (client.symbol_count > MAX_SYMBOLS) {
            client.symbol_count = MAX_SYMBOLS;
        }
        for (int i = 0; i < client.symbol_count; i++) {
            strncpy(client.symbols[i], argv[i + 3], sizeof(client.symbols[i]) - 1);
            client.symbols[i][sizeof(client.symbols[i]) - 1] = '\0';
        }
        printf("Direct symbols mode: streaming %d symbols\n", client.symbol_count);
    } else {
        // Default test mode
        strcpy(client.symbols[0], "FAKEPACA");
        client.symbol_count = 1;
        printf("Using test symbol: FAKEPACA\n");
    }
    
    signal(SIGINT, sigint_handler);
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    client.context = lws_create_context(&info);
    if (!client.context) {
        printf("Failed to create libwebsockets context\n");
        return 1;
    }
    
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = client.context;
    connect_info.address = "stream.data.alpaca.markets";
    connect_info.port = 443;
    
    connect_info.path = "/v1beta1/indicative";  // Use indicative feed for options data
    printf("Connecting to Alpaca options stream...\n");
    
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    connect_info.protocol = protocols[0].name;
    connect_info.ssl_connection = LCCSCF_USE_SSL |
                                 LCCSCF_ALLOW_SELFSIGNED |
                                 LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    
    printf("Endpoint: %s%s\n", connect_info.address, connect_info.path);
    client.wsi = lws_client_connect_via_info(&connect_info);
    if (!client.wsi) {
        printf("Failed to connect\n");
        lws_context_destroy(client.context);
        return 1;
    }
    
    //printf("Streaming options data for symbols:\n");
    //for (int i = 0; i < client.symbol_count; i++) {
      //  char readable_symbol[64];
        //parse_option_symbol(client.symbols[i], readable_symbol, sizeof(readable_symbol));
        //printf("  %s (%s)\n", readable_symbol, client.symbols[i]);
    //}
    //printf("Press Ctrl+C to exit\n\n");
    
    while (!client.interrupted && client.wsi) {
        lws_service(client.context, 50);
    }
    
    printf("\nShutting down...\n");
    
    if (client.wsi) {
        lws_close_reason(client.wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    }
    
    lws_context_destroy(client.context);
    
    // Cleanup curl
    curl_global_cleanup();
    
    return 0;
}