#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include "types.h"

// WebSocket callback function
int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                      void *user, void *in, size_t len);

// WebSocket connection management
int websocket_connect(alpaca_client_t *client);
void websocket_disconnect(alpaca_client_t *client);

// Dual WebSocket management (options + stocks)
int dual_websocket_connect(alpaca_client_t *client);
void dual_websocket_disconnect(alpaca_client_t *client);
int dual_websocket_service(alpaca_client_t *client, int timeout_ms);

// Message sending functions
void send_auth_message(struct lws *wsi, alpaca_client_t *client);
void send_subscription_message(struct lws *wsi, alpaca_client_t *client);

#endif // WEBSOCKET_H