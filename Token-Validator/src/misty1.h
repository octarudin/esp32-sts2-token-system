#pragma once

#include <stdint.h>

// Minimal MISTY1 implementation following RFC 2994.
// MISTY1 uses a 128-bit key and encrypts one 64-bit block.
class Misty1 {
 public:
  explicit Misty1(const uint8_t key[16]);
  uint64_t encryptBlock(uint64_t plaintext) const;
  uint64_t decryptBlock(uint64_t ciphertext) const;

 private:
  uint16_t expandedKey_[32]{};
  static uint16_t fi(uint16_t input, uint16_t key);
  uint32_t fo(uint32_t input, uint8_t round) const;
  uint32_t fl(uint32_t input, uint8_t round) const;
  uint32_t flInverse(uint32_t input, uint8_t round) const;
};
