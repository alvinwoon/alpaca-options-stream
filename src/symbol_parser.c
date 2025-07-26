#include "../include/symbol_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

void parse_option_symbol(const char *symbol, char *readable, size_t readable_size) {
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

option_details_t parse_option_details(const char *symbol) {
    option_details_t details = {0};
    
    if (!symbol) {
        details.is_valid = 0;
        return details;
    }
    
    size_t len = strlen(symbol);
    if (len < 15) {  // Minimum length for a valid option symbol
        details.is_valid = 0;
        return details;
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
        details.is_valid = 0;
        return details;
    }
    
    // Extract underlying symbol
    size_t underlying_len = date_start - symbol;
    if (underlying_len >= sizeof(details.underlying)) {
        underlying_len = sizeof(details.underlying) - 1;
    }
    strncpy(details.underlying, symbol, underlying_len);
    details.underlying[underlying_len] = '\0';
    
    // Extract expiry date (YYMMDD)
    strncpy(details.expiry_date, date_start, 6);
    details.expiry_date[6] = '\0';
    
    // Extract option type
    details.option_type = date_start[6];
    
    // Extract strike price (8 digits, divide by 1000 to get actual price)
    char strike_str[9];
    strncpy(strike_str, date_start + 7, 8);
    strike_str[8] = '\0';
    details.strike = atoi(strike_str) / 1000.0;
    
    details.is_valid = 1;
    return details;
}