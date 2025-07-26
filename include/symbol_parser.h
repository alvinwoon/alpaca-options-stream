#ifndef SYMBOL_PARSER_H
#define SYMBOL_PARSER_H

#include <stddef.h>

// Structure to hold parsed option details
typedef struct {
    char underlying[16];    // Underlying symbol (e.g., "AAPL")
    char expiry_date[7];    // Expiry in YYMMDD format (e.g., "241220")
    char option_type;       // 'C' for call, 'P' for put
    double strike;          // Strike price
    int is_valid;          // 1 if parsing was successful, 0 otherwise
} option_details_t;

// Parse option symbol into human-readable format
void parse_option_symbol(const char *symbol, char *readable, size_t readable_size);

// Parse option symbol and extract structured details
option_details_t parse_option_details(const char *symbol);

#endif // SYMBOL_PARSER_H