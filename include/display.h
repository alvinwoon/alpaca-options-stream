#ifndef DISPLAY_H
#define DISPLAY_H

#include "types.h"

// Display functions
void display_option_data(alpaca_client_t *client);
void display_symbols_list(alpaca_client_t *client, const char *title);

// Display threading functions
int start_display_thread(alpaca_client_t *client);
void stop_display_thread(alpaca_client_t *client);

#endif // DISPLAY_H