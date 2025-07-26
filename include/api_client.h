#ifndef API_CLIENT_H
#define API_CLIENT_H

#include "types.h"

// Callback for CURL responses
size_t api_response_callback(void *contents, size_t size, size_t nmemb, api_response_t *response);

// Fetch option symbols from REST API
int fetch_option_symbols(alpaca_client_t *client, const char *underlying_symbol, 
                        const char *exp_date_gte, const char *exp_date_lte, 
                        double strike_price_gte, double strike_price_lte);

#endif // API_CLIENT_H