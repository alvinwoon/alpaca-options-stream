#ifndef MOCK_DATA_H
#define MOCK_DATA_H

#include "types.h"

// Mock data generation functions
void start_mock_data_stream(alpaca_client_t *client);
void stop_mock_data_stream(void);
void generate_mock_trade(alpaca_client_t *client, const char *symbol);
void generate_mock_quote(alpaca_client_t *client, const char *symbol);

// Mock data configuration
void set_mock_data_interval(int milliseconds);
void set_mock_data_volatility(double volatility_factor);

#endif // MOCK_DATA_H