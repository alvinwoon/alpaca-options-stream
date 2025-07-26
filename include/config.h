#ifndef CONFIG_H
#define CONFIG_H

#define CONFIG_FILE_PATH "config.json"
#define CONFIG_EXAMPLE_PATH "config.example.json"
#define MAX_KEY_LENGTH 256

typedef struct {
    char alpaca_api_key[MAX_KEY_LENGTH];
    char alpaca_api_secret[MAX_KEY_LENGTH];
    char fred_api_key[MAX_KEY_LENGTH];
    int valid;
} app_config_t;

// Function declarations
int load_config(app_config_t *config);
int create_example_config(void);
void print_config_help(void);

#endif // CONFIG_H