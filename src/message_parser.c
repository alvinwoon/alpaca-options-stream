#include "../include/message_parser.h"
#include "../include/display.h"
#include "../include/websocket.h"
#include "../include/black_scholes.h"
#include "../include/symbol_parser.h"
#include "../include/stock_websocket.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>

option_data_t* find_or_create_option_data(const char *symbol, alpaca_client_t *client) {
    // First try to find existing entry
    for (int i = 0; i < client->data_count; i++) {
        if (strcmp(client->option_data[i].symbol, symbol) == 0) {
            return &client->option_data[i];
        }
    }
    
    // Create new entry if we have space
    if (client->data_count < MAX_SYMBOLS) {
        option_data_t *new_data = &client->option_data[client->data_count];
        memset(new_data, 0, sizeof(option_data_t));
        strncpy(new_data->symbol, symbol, sizeof(new_data->symbol) - 1);
        client->data_count++;
        return new_data;
    }
    
    return NULL;
}

const char* extract_string_from_msgpack(msgpack_object *obj) {
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

void calculate_option_analytics(option_data_t *data, alpaca_client_t *client) {
    if (!data || !client) return;
    
    // Rate limit analytics calculations - only update every 100ms per symbol
    static time_t last_calc_time[MAX_SYMBOLS];
    static int symbol_indices_initialized = 0;
    
    if (!symbol_indices_initialized) {
        memset(last_calc_time, 0, sizeof(last_calc_time));
        symbol_indices_initialized = 1;
    }
    
    // Find symbol index for rate limiting
    int symbol_idx = -1;
    for (int i = 0; i < client->data_count; i++) {
        if (strcmp(client->option_data[i].symbol, data->symbol) == 0) {
            symbol_idx = i;
            break;
        }
    }
    
    if (symbol_idx >= 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_t now_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;
        
        if (now_ms - last_calc_time[symbol_idx] < 100) { // 100ms throttle
            return; // Skip calculation
        }
        last_calc_time[symbol_idx] = now_ms;
    }
    
    // Parse the option symbol to extract details
    option_details_t details = parse_option_details(data->symbol);
    if (!details.is_valid) {
        data->analytics_valid = 0;
        return;
    }
    
    // Get underlying price from stock WebSocket data
    double underlying_price = get_underlying_price(client, details.underlying);
    if (underlying_price <= 0.0) {
        data->analytics_valid = 0;
        return;
    }
    
    // Calculate time to expiry
    double time_to_expiry = time_to_expiry_years(details.expiry_date);
    if (time_to_expiry <= 0.0) {
        data->analytics_valid = 0;
        return;
    }
    
    // Determine option price to use for IV calculation
    double option_price = 0.0;
    if (data->has_trade && data->last_price > 0.0) {
        // Use last trade price if available
        option_price = data->last_price;
    } else if (data->has_quote && data->bid_price > 0.0 && data->ask_price > 0.0) {
        // Use mid-price if no trade but have quote
        option_price = (data->bid_price + data->ask_price) / 2.0;
    } else {
        data->analytics_valid = 0;
        return;
    }
    
    // Store option details
    data->strike = details.strike;
    data->underlying_price = underlying_price;
    data->time_to_expiry = time_to_expiry;
    data->is_call = (details.option_type == 'C') ? 1 : 0;
    
    // Calculate Black-Scholes analytics
    data->bs_analytics = calculate_full_bs_metrics(
        underlying_price,           // S
        details.strike,            // K
        time_to_expiry,           // T
        client->risk_free_rate,   // r
        option_price,             // market_price
        data->is_call             // is_call
    );
    
    data->analytics_valid = 1;
}

void parse_option_trade(msgpack_object *trade_obj, alpaca_client_t *client) {
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
    
    // Filter out trades with less than 10 contracts to reduce noise
    
    
    if (strlen(symbol) > 0) {
        // Lock mutex before updating data
        pthread_mutex_lock(&client->data_mutex);
        
        option_data_t *data = find_or_create_option_data(symbol, client);
        if (data) {
            data->last_price = price;
            data->last_size = size;
            strncpy(data->trade_exchange, exchange, sizeof(data->trade_exchange) - 1);
            strncpy(data->trade_time, timestamp_str, sizeof(data->trade_time) - 1);
            strncpy(data->trade_condition, condition, sizeof(data->trade_condition) - 1);
            data->has_trade = 1;
            
            // Calculate Black-Scholes analytics
            calculate_option_analytics(data, client);
        }
        
        pthread_mutex_unlock(&client->data_mutex);
        // Note: Display thread handles rendering independently
    }
}

void parse_option_quote(msgpack_object *quote_obj, alpaca_client_t *client) {
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
        // Lock mutex before updating data
        pthread_mutex_lock(&client->data_mutex);
        
        option_data_t *data = find_or_create_option_data(symbol, client);
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
            
            // Calculate Black-Scholes analytics
            calculate_option_analytics(data, client);
        }
        
        pthread_mutex_unlock(&client->data_mutex);
        // Note: Display thread handles rendering independently
    }
}

void process_message(const char *data, size_t len, alpaca_client_t *client) {
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
                    if (strcmp(msg_type, "success") == 0) {
                        printf("Success: authenticated\n");
                        client->authenticated = 1;
                        if (!client->subscribed) {
                            send_subscription_message(client->wsi, client);
                            client->subscribed = 1;
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
                        parse_option_trade(item, client);
                    } else if (strcmp(msg_type, "q") == 0) {
                        parse_option_quote(item, client);
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
            if (strcmp(msg_type, "success") == 0) {
                printf("Success: authenticated\n");
                client->authenticated = 1;
                if (!client->subscribed) {
                    send_subscription_message(client->wsi, client);
                    client->subscribed = 1;
                }
            } else if (strcmp(msg_type, "error") == 0) {
                printf("Error received from server\n");
            } else if (strcmp(msg_type, "t") == 0) {
                parse_option_trade(&deserialized, client);
            } else if (strcmp(msg_type, "q") == 0) {
                parse_option_quote(&deserialized, client);
            }
        }
    }
    
    msgpack_zone_destroy(&mempool);
}