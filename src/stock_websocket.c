#include "../include/stock_websocket.h"
#include "../include/types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <cjson/cJSON.h>
#include <libwebsockets.h>

// stock_client_t is now defined in the header

static struct lws_protocols stock_protocols[] = {
    {
        "alpaca-stock-protocol",
        stock_websocket_callback,
        0,
        MAX_PAYLOAD,
    },
    { NULL, NULL, 0, 0 }
};

const char* extract_underlying_from_option(const char *option_symbol) {
    static char underlying[16];
    if (!option_symbol) return NULL;
    
    size_t len = strlen(option_symbol);
    if (len < 15) return NULL;
    
    // Find where the date starts (look for 6 consecutive digits + C/P + 8 digits)
    for (size_t i = 1; i <= len - 15; i++) {
        if (i + 14 < len &&
            isdigit(option_symbol[i]) && isdigit(option_symbol[i+1]) &&     // YY
            isdigit(option_symbol[i+2]) && isdigit(option_symbol[i+3]) &&   // MM  
            isdigit(option_symbol[i+4]) && isdigit(option_symbol[i+5]) &&   // DD
            (option_symbol[i+6] == 'C' || option_symbol[i+6] == 'P') &&     // Type
            isdigit(option_symbol[i+7])) {                                  // Strike start
            
            // Extract underlying symbol
            size_t underlying_len = i;
            if (underlying_len >= sizeof(underlying)) underlying_len = sizeof(underlying) - 1;
            strncpy(underlying, option_symbol, underlying_len);
            underlying[underlying_len] = '\0';
            return underlying;
        }
    }
    
    return NULL;
}

void extract_underlying_symbols(alpaca_client_t *client) {
    if (!client->stock_client) return;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    stock_client->underlying_count = 0;
    
    for (int i = 0; i < client->symbol_count; i++) {
        const char *underlying = extract_underlying_from_option(client->symbols[i]);
        if (!underlying) continue;
        
        // Check if already added
        int found = 0;
        for (int j = 0; j < stock_client->underlying_count; j++) {
            if (strcmp(stock_client->underlying_symbols[j], underlying) == 0) {
                found = 1;
                break;
            }
        }
        
        if (!found && stock_client->underlying_count < MAX_UNDERLYINGS) {
            strncpy(stock_client->underlying_symbols[stock_client->underlying_count], 
                   underlying, sizeof(stock_client->underlying_symbols[0]) - 1);
            stock_client->underlying_symbols[stock_client->underlying_count][sizeof(stock_client->underlying_symbols[0]) - 1] = '\0';
            stock_client->underlying_count++;
        }
    }
    
    printf("[STOCK] Extracted %d underlying symbols for tracking:\n", stock_client->underlying_count);
    for (int i = 0; i < stock_client->underlying_count; i++) {
        printf("[STOCK]   %s\n", stock_client->underlying_symbols[i]);
    }
}

void init_price_cache(alpaca_client_t *client) {
    if (!client->stock_client) return;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    for (int i = 0; i < MAX_UNDERLYINGS; i++) {
        memset(&stock_client->price_cache[i], 0, sizeof(underlying_price_t));
        pthread_rwlock_init(&stock_client->price_cache[i].lock, NULL);
    }
}

void cleanup_price_cache(alpaca_client_t *client) {
    if (!client->stock_client) return;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    for (int i = 0; i < MAX_UNDERLYINGS; i++) {
        pthread_rwlock_destroy(&stock_client->price_cache[i].lock);
    }
}

double get_underlying_price(alpaca_client_t *client, const char *symbol) {
    if (!client->stock_client || !symbol) return 0.0;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    for (int i = 0; i < MAX_UNDERLYINGS; i++) {
        if (strcmp(stock_client->price_cache[i].symbol, symbol) == 0) {
            pthread_rwlock_rdlock(&stock_client->price_cache[i].lock);
            double price = stock_client->price_cache[i].last_price;
            int valid = stock_client->price_cache[i].is_valid;
            pthread_rwlock_unlock(&stock_client->price_cache[i].lock);
            
            return valid ? price : 0.0;
        }
    }
    
    return 0.0;
}

int update_underlying_price(alpaca_client_t *client, const char *symbol, 
                           double price, const char *timestamp) {
    if (!client->stock_client || !symbol) return 0;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    // Find existing entry or create new one
    int index = -1;
    for (int i = 0; i < MAX_UNDERLYINGS; i++) {
        if (strcmp(stock_client->price_cache[i].symbol, symbol) == 0) {
            index = i;
            break;
        }
    }
    
    // Create new entry if not found
    if (index == -1) {
        for (int i = 0; i < MAX_UNDERLYINGS; i++) {
            if (!stock_client->price_cache[i].is_valid) {
                index = i;
                strncpy(stock_client->price_cache[i].symbol, symbol, 
                       sizeof(stock_client->price_cache[i].symbol) - 1);
                stock_client->price_cache[i].symbol[sizeof(stock_client->price_cache[i].symbol) - 1] = '\0';
                break;
            }
        }
    }
    
    if (index == -1) return 0; // No space available
    
    // Update the entry
    pthread_rwlock_wrlock(&stock_client->price_cache[index].lock);
    stock_client->price_cache[index].last_price = price;
    if (timestamp) {
        strncpy(stock_client->price_cache[index].timestamp, timestamp, 
               sizeof(stock_client->price_cache[index].timestamp) - 1);
        stock_client->price_cache[index].timestamp[sizeof(stock_client->price_cache[index].timestamp) - 1] = '\0';
    }
    stock_client->price_cache[index].is_valid = 1;
    pthread_rwlock_unlock(&stock_client->price_cache[index].lock);
    
    return 1;
}

void send_stock_auth_message(struct lws *wsi, alpaca_client_t *client) {
    cJSON *auth_json = cJSON_CreateObject();
    cJSON *action = cJSON_CreateString("auth");
    cJSON *key = cJSON_CreateString(client->api_key);
    cJSON *secret = cJSON_CreateString(client->api_secret);
    
    cJSON_AddItemToObject(auth_json, "action", action);
    cJSON_AddItemToObject(auth_json, "key", key);
    cJSON_AddItemToObject(auth_json, "secret", secret);
    
    char *json_string = cJSON_Print(auth_json);
    size_t json_len = strlen(json_string);
    
    unsigned char buf[LWS_PRE + json_len];
    memcpy(&buf[LWS_PRE], json_string, json_len);
    
    lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    printf("[STOCK] Sent authentication message (JSON)\n");
    
    free(json_string);
    cJSON_Delete(auth_json);
}

void send_stock_subscription_message(struct lws *wsi, alpaca_client_t *client) {
    if (!client->stock_client) return;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    cJSON *sub_json = cJSON_CreateObject();
    cJSON *action = cJSON_CreateString("subscribe");
    cJSON *trades = cJSON_CreateArray();
    cJSON *quotes = cJSON_CreateArray();
    
    // Add underlying symbols to both trades and quotes
    for (int i = 0; i < stock_client->underlying_count; i++) {
        cJSON *trade_symbol = cJSON_CreateString(stock_client->underlying_symbols[i]);
        cJSON *quote_symbol = cJSON_CreateString(stock_client->underlying_symbols[i]);
        cJSON_AddItemToArray(trades, trade_symbol);
        cJSON_AddItemToArray(quotes, quote_symbol);
    }
    
    cJSON_AddItemToObject(sub_json, "action", action);
    cJSON_AddItemToObject(sub_json, "trades", trades);
    cJSON_AddItemToObject(sub_json, "quotes", quotes);
    
    char *json_string = cJSON_Print(sub_json);
    size_t json_len = strlen(json_string);
    
    unsigned char buf[LWS_PRE + json_len];
    memcpy(&buf[LWS_PRE], json_string, json_len);
    
    lws_write(wsi, &buf[LWS_PRE], json_len, LWS_WRITE_TEXT);
    
    printf("[STOCK] Sent subscription for %d underlying symbols (JSON)\n", stock_client->underlying_count);
    
    free(json_string);
    cJSON_Delete(sub_json);
}

void process_stock_message(const char *data, size_t len, alpaca_client_t *client) {
    char *json_data = malloc(len + 1);
    memcpy(json_data, data, len);
    json_data[len] = '\0';
    
    cJSON *json = cJSON_Parse(json_data);
    if (!json) {
        printf("Failed to parse stock JSON message\n");
        free(json_data);
        return;
    }
    
    // Handle array of messages
    if (cJSON_IsArray(json)) {
        int array_size = cJSON_GetArraySize(json);
        for (int i = 0; i < array_size; i++) {
            cJSON *item = cJSON_GetArrayItem(json, i);
            if (!item) continue;
            
            cJSON *type = cJSON_GetObjectItem(item, "T");
            if (!type || !cJSON_IsString(type)) continue;
            
            const char *msg_type = type->valuestring;
            
            if (strcmp(msg_type, "success") == 0) {
                printf("[STOCK] WebSocket authenticated successfully\n");
                if (client->stock_client) {
                    ((stock_client_t *)client->stock_client)->stock_authenticated = 1;
                    if (!((stock_client_t *)client->stock_client)->stock_subscribed) {
                        send_stock_subscription_message(((stock_client_t *)client->stock_client)->stock_wsi, client);
                        ((stock_client_t *)client->stock_client)->stock_subscribed = 1;
                    }
                }
            } else if (strcmp(msg_type, "subscription") == 0) {
                printf("[STOCK] Subscription confirmed\n");
            } else if (strcmp(msg_type, "t") == 0) {
                // Trade message
                cJSON *symbol = cJSON_GetObjectItem(item, "S");
                cJSON *price = cJSON_GetObjectItem(item, "p");
                cJSON *timestamp = cJSON_GetObjectItem(item, "t");
                
                if (symbol && cJSON_IsString(symbol) && price && cJSON_IsNumber(price)) {
                    const char *timestamp_str = (timestamp && cJSON_IsString(timestamp)) ? timestamp->valuestring : NULL;
                    update_underlying_price(client, symbol->valuestring, price->valuedouble, timestamp_str);
                    printf("[STOCK] Trade: %s @ $%.4f\n", symbol->valuestring, price->valuedouble);
                }
            } else if (strcmp(msg_type, "q") == 0) {
                // Quote message - we can also use bid/ask prices
                cJSON *symbol = cJSON_GetObjectItem(item, "S");
                cJSON *bid_price = cJSON_GetObjectItem(item, "bp");
                cJSON *ask_price = cJSON_GetObjectItem(item, "ap");
                
                if (symbol && cJSON_IsString(symbol) && 
                    bid_price && cJSON_IsNumber(bid_price) &&
                    ask_price && cJSON_IsNumber(ask_price)) {
                    
                    // Use mid-price if no recent trade
                    double mid_price = (bid_price->valuedouble + ask_price->valuedouble) / 2.0;
                    double current_price = get_underlying_price(client, symbol->valuestring);
                    
                    if (current_price == 0.0) {
                        // No trade price available, use mid-price
                        update_underlying_price(client, symbol->valuestring, mid_price, NULL);
                        printf("[STOCK] Quote: %s Mid: $%.4f (Bid: $%.4f, Ask: $%.4f)\n", 
                               symbol->valuestring, mid_price, bid_price->valuedouble, ask_price->valuedouble);
                    }
                }
            }
        }
    }
    
    cJSON_Delete(json);
    free(json_data);
}

int stock_websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                            void *user, void *in, size_t len) {
    alpaca_client_t *client = (alpaca_client_t *)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("[STOCK] WebSocket connection established\n");
            send_stock_auth_message(wsi, client);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            process_stock_message((const char*)in, len, client);
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("[STOCK] WebSocket connection error\n");
            if (client->stock_client) {
                ((stock_client_t *)client->stock_client)->stock_wsi = NULL;
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("[STOCK] WebSocket connection closed\n");
            if (client->stock_client) {
                ((stock_client_t *)client->stock_client)->stock_wsi = NULL;
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            break;
            
        default:
            break;
    }
    
    return 0;
}

int stock_websocket_connect(alpaca_client_t *client) {
    if (!client) return 0;
    
    // Allocate stock client
    client->stock_client = malloc(sizeof(stock_client_t));
    if (!client->stock_client) {
        printf("[STOCK] Failed to allocate stock client\n");
        return 0;
    }
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    memset(stock_client, 0, sizeof(stock_client_t));
    
    // Initialize price cache
    init_price_cache(client);
    
    // Extract underlying symbols from option symbols
    extract_underlying_symbols(client);
    
    if (stock_client->underlying_count == 0) {
        printf("[STOCK] No underlying symbols found, skipping stock WebSocket\n");
        return 1; // Not an error, just no stocks to track
    }
    
    // Create WebSocket context for stocks
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = stock_protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = client;
    
    stock_client->stock_context = lws_create_context(&info);
    if (!stock_client->stock_context) {
        printf("[STOCK] Failed to create stock WebSocket context\n");
        free(client->stock_client);
        client->stock_client = NULL;
        return 0;
    }
    
    // Connect to stock WebSocket
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = stock_client->stock_context;
    connect_info.address = "stream.data.alpaca.markets";
    connect_info.port = 443;
    connect_info.path = "/v2/iex";  // Stock WebSocket endpoint
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    connect_info.protocol = stock_protocols[0].name;
    connect_info.ssl_connection = LCCSCF_USE_SSL |
                                 LCCSCF_ALLOW_SELFSIGNED |
                                 LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    
    printf("[STOCK] Endpoint: %s%s\n", connect_info.address, connect_info.path);
    
    stock_client->stock_wsi = lws_client_connect_via_info(&connect_info);
    if (!stock_client->stock_wsi) {
        printf("[STOCK] Failed to connect to stock WebSocket\n");
        lws_context_destroy(stock_client->stock_context);
        free(client->stock_client);
        client->stock_client = NULL;
        return 0;
    }
    
    return 1;
}

// Initialize stock client for mock mode (no WebSocket connection)
int init_stock_client_for_mock(alpaca_client_t *client) {
    if (!client) return 0;
    
    // Allocate stock client
    client->stock_client = malloc(sizeof(stock_client_t));
    if (!client->stock_client) {
        printf("[STOCK] Failed to allocate stock client for mock mode\n");
        return 0;
    }
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    memset(stock_client, 0, sizeof(stock_client_t));
    
    // Initialize price cache
    init_price_cache(client);
    
    // Extract underlying symbols from option symbols
    extract_underlying_symbols(client);
    
    printf("[STOCK] Mock mode: initialized stock client for %d underlying symbols\n", 
           stock_client->underlying_count);
    
    return 1;
}

void stock_websocket_disconnect(alpaca_client_t *client) {
    if (!client->stock_client) return;
    
    stock_client_t *stock_client = (stock_client_t *)client->stock_client;
    
    if (stock_client->stock_wsi) {
        lws_close_reason(stock_client->stock_wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    }
    
    if (stock_client->stock_context) {
        lws_context_destroy(stock_client->stock_context);
    }
    
    cleanup_price_cache(client);
    free(client->stock_client);
    client->stock_client = NULL;
}