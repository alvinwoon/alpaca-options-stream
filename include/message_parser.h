#ifndef MESSAGE_PARSER_H
#define MESSAGE_PARSER_H

#include "types.h"
#include <msgpack.h>

// Message parsing functions
void process_message(const char *data, size_t len, alpaca_client_t *client);
const char* extract_string_from_msgpack(msgpack_object *obj);

// Option data parsing functions
void parse_option_trade(msgpack_object *trade_obj, alpaca_client_t *client);
void parse_option_quote(msgpack_object *quote_obj, alpaca_client_t *client);

// Data management
option_data_t* find_or_create_option_data(const char *symbol, alpaca_client_t *client);

// Analytics calculation
void calculate_option_analytics(option_data_t *data, alpaca_client_t *client);

#endif // MESSAGE_PARSER_H