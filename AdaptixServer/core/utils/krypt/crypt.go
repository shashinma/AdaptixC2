package krypt

import (
	"crypto/aes"
	"crypto/cipher"
	"errors"
)

// RC4Crypt is deprecated; kept for compatibility with old plugins
func RC4Crypt(data []byte, key []byte) ([]byte, error) {
	return nil, errors.New("rc4 is deprecated")
}

// AesCtrEncrypt encrypts data using AES-128-CTR with the given IV.
func AesCtrEncrypt(data []byte, key []byte, iv []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	stream := cipher.NewCTR(block, iv)
	encrypted := make([]byte, len(data))
	stream.XORKeyStream(encrypted, data)
	return encrypted, nil
}

// AesCtrDecrypt decrypts data using AES-128-CTR with the given IV.
// CTR mode encryption and decryption are identical operations.
func AesCtrDecrypt(data []byte, key []byte, iv []byte) ([]byte, error) {
	return AesCtrEncrypt(data, key, iv)
}

// AesCtrEncryptWithIV prepends a random IV and encrypts data using AES-128-CTR.
// Returns [IV:16][encrypted:N].
func AesCtrEncryptWithIV(data []byte, key []byte) ([]byte, error) {
	if len(key) != 16 {
		return nil, errors.New("invalid key size for AES-128")
	}
	iv := GenerateSlice(16)
	if iv == nil {
		return nil, errors.New("failed to generate IV")
	}
	encrypted, err := AesCtrEncrypt(data, key, iv)
	if err != nil {
		return nil, err
	}
	return append(iv, encrypted...), nil
}

// AesCtrDecryptWithIV decrypts data that has a prepended IV: [IV:16][encrypted:N].
func AesCtrDecryptWithIV(data []byte, key []byte) ([]byte, error) {
	if len(data) < 16 {
		return nil, errors.New("data too short for IV")
	}
	if len(key) != 16 {
		return nil, errors.New("invalid key size for AES-128")
	}
	iv := data[:16]
	ciphertext := data[16:]
	return AesCtrDecrypt(ciphertext, key, iv)
}
