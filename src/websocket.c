#include "../include/websocket.h"
#include "../include/stock_websocket.h"
#include "../include/message_parser.h"
#include "../include/display.h"
#include <stdio.h>
#include <string.h>
#include <msgpack.h>

static struct lws_protocols protocols[] = {
    {
        "alpaca-options-protocol",
        websocket_callback,
        0,
        MAX_PAYLOAD,
    },
    { NULL, NULL, 0, 0 }
};

int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason,
                      void *user, void *in, size_t len) {
    // We need to access the client from somewhere - we'll pass it through user data
    alpaca_client_t *client = (alpaca_client_t *)lws_context_user(lws_get_context(wsi));
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf("[OPTIONS] WebSocket connection established\n");
            send_auth_message(wsi, client);
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            process_message((const char*)in, len, client);
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            printf("Connection error\n");
            client->wsi = NULL;
            break;
            
        case LWS_CALLBACK_CLOSED:
            printf("Connection closed\n");
            client->wsi = NULL;
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            break;
            
        default:
            break;
    }
    
    return 0;
}

int websocket_connect(alpaca_client_t *client) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.user = client; // Pass client as user data
    
    client->context = lws_create_context(&info);
    if (!client->context) {
        printf("Failed to create libwebsockets context\n");
        return 0;
    }
    
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = client->context;
    connect_info.address = "stream.data.alpaca.markets";
    connect_info.port = 443;
    connect_info.path = "/v1beta1/indicative";
    connect_info.host = connect_info.address;
    connect_info.origin = connect_info.address;
    connect_info.protocol = protocols[0].name;
    connect_info.ssl_connection = LCCSCF_USE_SSL |
                                 LCCSCF_ALLOW_SELFSIGNED |
                                 LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    
    printf("Connecting to Alpaca options stream...\n");
    printf("Endpoint: %s%s\n", connect_info.address, connect_info.path);
    
    client->wsi = lws_client_connect_via_info(&connect_info);
    if (!client->wsi) {
        printf("Failed to connect\n");
        lws_context_destroy(client->context);
        return 0;
    }
    
    return 1;
}

void websocket_disconnect(alpaca_client_t *client) {
    if (client->wsi) {
        lws_close_reason(client->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    }
    
    if (client->context) {
        lws_context_destroy(client->context);
    }
}

void send_auth_message(struct lws *wsi, alpaca_client_t *client) {
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
    size_t key_len = strlen(client->api_key);
    msgpack_pack_str(&pk, key_len);
    msgpack_pack_str_body(&pk, client->api_key, key_len);
    
    // "secret": api_secret
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "secret", 6);
    size_t secret_len = strlen(client->api_secret);
    msgpack_pack_str(&pk, secret_len);
    msgpack_pack_str_body(&pk, client->api_secret, secret_len);
    
    unsigned char buf[LWS_PRE + sbuf.size];
    memcpy(&buf[LWS_PRE], sbuf.data, sbuf.size);
    
    lws_write(wsi, &buf[LWS_PRE], sbuf.size, LWS_WRITE_BINARY);
    
    printf("[OPTIONS] Sent authentication message (MsgPack)\n");
    
    msgpack_sbuffer_destroy(&sbuf);
}

void send_subscription_message(struct lws *wsi, alpaca_client_t *client) {
    msgpack_sbuffer sbuf;
    msgpack_packer pk;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);
    
    // Pack subscription message as map with 2 elements (trades only)
    msgpack_pack_map(&pk, 2);
    
    // "action": "subscribe"
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "action", 6);
    msgpack_pack_str(&pk, 9);
    msgpack_pack_str_body(&pk, "subscribe", 9);
    
    // "trades": [symbols...]
    msgpack_pack_str(&pk, 6);
    msgpack_pack_str_body(&pk, "trades", 6);
    msgpack_pack_array(&pk, client->symbol_count);
    for (int i = 0; i < client->symbol_count; i++) {
        size_t sym_len = strlen(client->symbols[i]);
        msgpack_pack_str(&pk, sym_len);
        msgpack_pack_str_body(&pk, client->symbols[i], sym_len);
    }
    
    unsigned char buf[LWS_PRE + sbuf.size];
    memcpy(&buf[LWS_PRE], sbuf.data, sbuf.size);
    
    lws_write(wsi, &buf[LWS_PRE], sbuf.size, LWS_WRITE_BINARY);
    
    printf("[OPTIONS] Sent subscription message for %d symbols - trades only (MsgPack, %lu bytes)\n", 
           client->symbol_count, sbuf.size);
    
    display_symbols_list(client, "Subscribed symbols");
    
    msgpack_sbuffer_destroy(&sbuf);
}

// Dual WebSocket functions
int dual_websocket_connect(alpaca_client_t *client) {
    printf("=== Connecting to dual WebSocket streams ===\n");
    
    // Connect to options WebSocket first
    printf("Connecting to OPTIONS WebSocket...\n");
    if (!websocket_connect(client)) {
        printf("❌ Failed to connect to OPTIONS WebSocket\n");
        return 0;
    }
    printf("✅ OPTIONS WebSocket connected successfully\n");
    
    // Connect to stock WebSocket
    printf("Connecting to STOCK WebSocket...\n");
    if (!stock_websocket_connect(client)) {
        printf("⚠️  Failed to connect to STOCK WebSocket (continuing with options only)\n");
        // Don't fail completely - options can work without stock data
    } else {
        printf("✅ STOCK WebSocket connected successfully\n");
    }
    
    return 1;
}

void dual_websocket_disconnect(alpaca_client_t *client) {
    printf("Disconnecting dual WebSocket streams...\n");
    
    // Disconnect stock WebSocket
    stock_websocket_disconnect(client);
    
    // Disconnect options WebSocket
    websocket_disconnect(client);
}

int dual_websocket_service(alpaca_client_t *client, int timeout_ms) {
    int ret = 0;
    
    // Service options WebSocket
    if (client->context) {
        ret += lws_service(client->context, timeout_ms / 2);
    }
    
    // Service stock WebSocket
    if (client->stock_client) {
        stock_client_t *stock_client = (stock_client_t *)client->stock_client;
        if (stock_client->stock_context) {
            ret += lws_service(stock_client->stock_context, timeout_ms / 2);
        }
    }
    
    return ret;
}