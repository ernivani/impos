#ifndef _KERNEL_HASH_H
#define _KERNEL_HASH_H

#include <stdint.h>
#include <stddef.h>

#define HASH_SALT_SIZE 16
#define HASH_OUTPUT_SIZE 32

/* Generate a random salt */
void hash_generate_salt(uint8_t* salt, size_t size);

/* Hash a password with a salt (output must be HASH_OUTPUT_SIZE bytes) */
void hash_password(const char* password, const uint8_t* salt, uint8_t* output);

/* Verify a password against a hash */
int hash_verify(const char* password, const uint8_t* salt, const uint8_t* expected_hash);

/* Convert hash to hex string for storage */
void hash_to_hex(const uint8_t* hash, size_t hash_len, char* hex_output, size_t hex_size);

/* Convert hex string back to hash */
void hex_to_hash(const char* hex, uint8_t* hash, size_t hash_len);

#endif
