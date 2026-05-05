#pragma once

#include <windows.h>

// Crypto type constants
#define CRYPTO_RC4 0
#define CRYPTO_AES 1

// RC4 encryption/decryption (deprecated but kept for compatibility)
void EncryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength);
void DecryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength);

// AES-128-CTR encryption/decryption
// CTR mode: encryption and decryption are the same operation (XOR with keystream)
void AesCtrEncryptBuffer(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, const BYTE* iv, int ivLen);

// Allocate and encrypt with random IV prepended: [IV:16][encrypted:N]
// Returns TRUE on success. Caller must MemFreeLocal the output buffer.
BOOL AesCtrEncryptAlloc(const BYTE* plaintext, ULONG plainLen, const BYTE* key, int keyLen, BYTE** outBuf, ULONG* outLen);

// Decrypt [IV:16][encrypted:N] in-place. Returns pointer to plaintext (data+16) and length.
BOOL AesCtrDecryptInPlace(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, BYTE** outPlain, ULONG* outPlainLen);

// Conditional crypto wrappers (use CRYPTO_RC4 or CRYPTO_AES)
void CryptoEncryptBuffer(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, int cryptoType);
BOOL CryptoEncryptAlloc(const BYTE* plaintext, ULONG plainLen, const BYTE* key, int keyLen, int cryptoType, BYTE** outBuf, ULONG* outLen);
BOOL CryptoDecryptInPlace(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, int cryptoType, BYTE** outPlain, ULONG* outPlainLen);

// Get current crypto type from global agent config
int GetCurrentCryptoType(void);
