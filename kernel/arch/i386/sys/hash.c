#include <kernel/hash.h>
#include <kernel/config.h>
#include <string.h>

/* Simple but cryptographically reasonable hash function */
static uint32_t hash_round(uint32_t state, uint8_t byte) {
    state ^= byte;
    state *= 0x5bd1e995;
    state ^= state >> 15;
    return state;
}

void hash_generate_salt(uint8_t* salt, size_t size) {
    /* Use system time as entropy source */
    datetime_t dt;
    config_get_datetime(&dt);
    uint32_t seed = dt.year * 31536000 + dt.month * 2592000 + 
                    dt.day * 86400 + dt.hour * 3600 + 
                    dt.minute * 60 + dt.second;
    
    /* Add config as more entropy */
    system_config_t* cfg = config_get();
    seed ^= cfg->uptime_seconds;
    
    /* Generate pseudo-random salt */
    for (size_t i = 0; i < size; i++) {
        seed = hash_round(seed, i);
        salt[i] = (uint8_t)(seed & 0xFF);
        seed = hash_round(seed, (seed >> 8) & 0xFF);
    }
}

void hash_password(const char* password, const uint8_t* salt, uint8_t* output) {
    uint32_t state[8];
    
    /* Initialize state with salt */
    for (int i = 0; i < 8; i++) {
        state[i] = 0x6a09e667 + (salt[i % HASH_SALT_SIZE] << 8) + salt[(i + 1) % HASH_SALT_SIZE];
    }
    
    /* Multiple passes for security */
    for (int pass = 0; pass < 1000; pass++) {
        /* Mix in password */
        const char* p = password;
        while (*p) {
            for (int i = 0; i < 8; i++) {
                state[i] = hash_round(state[i], *p);
            }
            p++;
        }
        
        /* Mix in salt */
        for (int i = 0; i < HASH_SALT_SIZE; i++) {
            state[i % 8] = hash_round(state[i % 8], salt[i]);
        }
        
        /* Mix states together */
        for (int i = 0; i < 8; i++) {
            state[i] ^= state[(i + 1) % 8];
            state[i] = hash_round(state[i], (pass + i) & 0xFF);
        }
    }
    
    /* Output hash */
    for (int i = 0; i < 8 && i * 4 < HASH_OUTPUT_SIZE; i++) {
        output[i * 4 + 0] = (state[i] >> 0) & 0xFF;
        output[i * 4 + 1] = (state[i] >> 8) & 0xFF;
        output[i * 4 + 2] = (state[i] >> 16) & 0xFF;
        output[i * 4 + 3] = (state[i] >> 24) & 0xFF;
    }
}

int hash_verify(const char* password, const uint8_t* salt, const uint8_t* expected_hash) {
    uint8_t computed_hash[HASH_OUTPUT_SIZE];
    hash_password(password, salt, computed_hash);
    
    /* Constant-time comparison to prevent timing attacks */
    int result = 0;
    for (int i = 0; i < HASH_OUTPUT_SIZE; i++) {
        result |= computed_hash[i] ^ expected_hash[i];
    }
    
    return result == 0 ? 0 : -1;
}

void hash_to_hex(const uint8_t* hash, size_t hash_len, char* hex_output, size_t hex_size) {
    const char hex_chars[] = "0123456789abcdef";
    size_t pos = 0;
    
    for (size_t i = 0; i < hash_len && pos + 2 < hex_size; i++) {
        hex_output[pos++] = hex_chars[(hash[i] >> 4) & 0x0F];
        hex_output[pos++] = hex_chars[hash[i] & 0x0F];
    }
    hex_output[pos] = '\0';
}

void hex_to_hash(const char* hex, uint8_t* hash, size_t hash_len) {
    for (size_t i = 0; i < hash_len; i++) {
        uint8_t high = 0, low = 0;
        
        char h = hex[i * 2];
        if (h >= '0' && h <= '9') high = h - '0';
        else if (h >= 'a' && h <= 'f') high = h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') high = h - 'A' + 10;
        
        char l = hex[i * 2 + 1];
        if (l >= '0' && l <= '9') low = l - '0';
        else if (l >= 'a' && l <= 'f') low = l - 'a' + 10;
        else if (l >= 'A' && l <= 'F') low = l - 'A' + 10;
        
        hash[i] = (high << 4) | low;
    }
}
