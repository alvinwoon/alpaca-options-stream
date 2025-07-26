#include "../include/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cjson.h>
#include <sys/stat.h>
#include <unistd.h>

int load_config(app_config_t *config) {
    if (!config) return 0;
    
    // Initialize config
    memset(config, 0, sizeof(app_config_t));
    config->valid = 0;
    
    // Check if config file exists
    if (access(CONFIG_FILE_PATH, F_OK) != 0) {
        printf("❌ Config file '%s' not found.\n", CONFIG_FILE_PATH);
        printf("Please create it from the example:\n");
        printf("  cp %s %s\n", CONFIG_EXAMPLE_PATH, CONFIG_FILE_PATH);
        printf("  # Edit %s with your API keys\n\n", CONFIG_FILE_PATH);
        return 0;
    }
    
    // Read file
    FILE *file = fopen(CONFIG_FILE_PATH, "r");
    if (!file) {
        printf("❌ Error: Cannot open config file '%s'\n", CONFIG_FILE_PATH);
        return 0;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0) {
        printf("❌ Error: Config file '%s' is empty\n", CONFIG_FILE_PATH);
        fclose(file);
        return 0;
    }
    
    // Read file content
    char *json_string = malloc(file_size + 1);
    if (!json_string) {
        printf("❌ Error: Memory allocation failed\n");
        fclose(file);
        return 0;
    }
    
    size_t read_size = fread(json_string, 1, file_size, file);
    json_string[read_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON *json = cJSON_Parse(json_string);
    free(json_string);
    
    if (!json) {
        printf("❌ Error: Invalid JSON in config file '%s'\n", CONFIG_FILE_PATH);
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            printf("JSON Error: %s\n", error_ptr);
        }
        return 0;
    }
    
    // Extract API keys
    cJSON *alpaca_key = cJSON_GetObjectItemCaseSensitive(json, "alpaca_api_key");
    cJSON *alpaca_secret = cJSON_GetObjectItemCaseSensitive(json, "alpaca_api_secret");
    cJSON *fred_key = cJSON_GetObjectItemCaseSensitive(json, "fred_api_key");
    
    // Validate required fields
    if (!cJSON_IsString(alpaca_key) || !cJSON_IsString(alpaca_secret)) {
        printf("❌ Error: Missing or invalid 'alpaca_api_key' or 'alpaca_api_secret' in config file\n");
        cJSON_Delete(json);
        return 0;
    }
    
    // Copy values
    strncpy(config->alpaca_api_key, alpaca_key->valuestring, MAX_KEY_LENGTH - 1);
    strncpy(config->alpaca_api_secret, alpaca_secret->valuestring, MAX_KEY_LENGTH - 1);
    
    // FRED API key is optional
    if (cJSON_IsString(fred_key) && strlen(fred_key->valuestring) > 0) {
        strncpy(config->fred_api_key, fred_key->valuestring, MAX_KEY_LENGTH - 1);
    } else {
        strcpy(config->fred_api_key, ""); // Use empty string to indicate no FRED key
    }
    
    // Ensure null termination
    config->alpaca_api_key[MAX_KEY_LENGTH - 1] = '\0';
    config->alpaca_api_secret[MAX_KEY_LENGTH - 1] = '\0';
    config->fred_api_key[MAX_KEY_LENGTH - 1] = '\0';
    
    cJSON_Delete(json);
    
    // Validate key lengths
    if (strlen(config->alpaca_api_key) < 10 || strlen(config->alpaca_api_secret) < 10) {
        printf("❌ Error: Alpaca API keys appear to be too short (less than 10 characters)\n");
        return 0;
    }
    
    config->valid = 1;
    printf("✅ Configuration loaded successfully\n");
    printf("   • Alpaca API Key: %.*s...\n", 8, config->alpaca_api_key);
    printf("   • Alpaca Secret: %.*s...\n", 8, config->alpaca_api_secret);
    if (strlen(config->fred_api_key) > 0) {
        printf("   • FRED API Key: %.*s...\n", 8, config->fred_api_key);
    } else {
        printf("   • FRED API Key: (not provided - will use default rate)\n");
    }
    printf("\n");
    
    return 1;
}

int create_example_config(void) {
    // Check if example already exists
    if (access(CONFIG_EXAMPLE_PATH, F_OK) == 0) {
        printf("✅ Example config file '%s' already exists\n", CONFIG_EXAMPLE_PATH);
        return 1;
    }
    
    FILE *file = fopen(CONFIG_EXAMPLE_PATH, "w");
    if (!file) {
        printf("❌ Error: Cannot create example config file '%s'\n", CONFIG_EXAMPLE_PATH);
        return 0;
    }
    
    fprintf(file, "{\n");
    fprintf(file, "  \"_comment\": \"Copy this file to config.json and add your API keys\",\n");
    fprintf(file, "  \"alpaca_api_key\": \"YOUR_ALPACA_API_KEY_HERE\",\n");
    fprintf(file, "  \"alpaca_api_secret\": \"YOUR_ALPACA_API_SECRET_HERE\",\n");
    fprintf(file, "  \"fred_api_key\": \"YOUR_FRED_API_KEY_HERE_OPTIONAL\"\n");
    fprintf(file, "}\n");
    
    fclose(file);
    
    printf("✅ Created example config file '%s'\n", CONFIG_EXAMPLE_PATH);
    return 1;
}

void print_config_help(void) {
    printf("\n=== API Configuration Help ===\n\n");
    
    printf("This application requires API keys to function. Please set them up:\n\n");
    
    printf("1. Create config file:\n");
    printf("   cp %s %s\n\n", CONFIG_EXAMPLE_PATH, CONFIG_FILE_PATH);
    
    printf("2. Edit %s and add your API keys:\n\n", CONFIG_FILE_PATH);
    
    printf("📊 ALPACA API KEYS (Required):\n");
    printf("   • Sign up at: https://alpaca.markets/\n");
    printf("   • Go to: Paper Trading -> API Keys\n");
    printf("   • Create new API key pair\n");
    printf("   • Add both 'alpaca_api_key' and 'alpaca_api_secret'\n\n");
    
    printf("📈 FRED API KEY (Optional):\n");
    printf("   • Sign up at: https://fred.stlouisfed.org/docs/api/api_key.html\n");
    printf("   • Get free API key for risk-free rate data\n");
    printf("   • Add as 'fred_api_key' (if not provided, uses default rate)\n\n");
    
    printf("3. The config.json file will be gitignored for security\n\n");
    
    printf("Example config.json:\n");
    printf("{\n");
    printf("  \"alpaca_api_key\": \"PKTEST1234567890ABCDEF\",\n"); 
    printf("  \"alpaca_api_secret\": \"SECRET1234567890ABCDEFGHIJK\",\n");
    printf("  \"fred_api_key\": \"abcdef1234567890\" \n");
    printf("}\n\n");
}