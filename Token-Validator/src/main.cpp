#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <Adafruit_TinyUSB.h>
#include <InternalFileSystem.h>

#include "misty1.h"

// EDUCATIONAL DEMO ONLY - not an STS-certified meter implementation.
// This validator intentionally mirrors the companion Token-Generator demo.
namespace {

using namespace Adafruit_LittleFS_Namespace;

constexpr uint8_t TOKEN_CLASS_CREDIT = 0;
constexpr uint8_t TOKEN_SUBCLASS_ELECTRICITY = 0;
constexpr uint32_t MAX_TID = 0x00ffffffUL;
constexpr uint32_t STATE_MAGIC = 0x53545332UL;  // "STS2"
constexpr uint16_t STATE_VERSION = 1;
constexpr char STATE_FILE[] = "/sts2-state.bin";
constexpr char STATE_TEMP_FILE[] = "/sts2-state.tmp";

// Demo key shared with Token-Generator. Never use a shared source-code key
// in a real vending or metering system.
constexpr uint8_t DEMO_MISTY1_KEY[16] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

struct TokenFields {
  uint8_t tokenClass;
  uint8_t subclass;
  uint8_t random;
  uint32_t tid;
  uint16_t amount;
  uint16_t crc;
};

struct PersistentState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t lastUsedTid;
  uint64_t balanceTenthsKwh;
  uint16_t checksum;
};

PersistentState state{STATE_MAGIC, STATE_VERSION, 0, 0, 0, 0};
bool storageReady = false;

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                            : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t calculateTokenCrc(const TokenFields& fields) {
  const uint8_t bytes[] = {
      fields.tokenClass,
      fields.subclass,
      fields.random,
      static_cast<uint8_t>(fields.tid >> 16),
      static_cast<uint8_t>(fields.tid >> 8),
      static_cast<uint8_t>(fields.tid),
      static_cast<uint8_t>(fields.amount >> 8),
      static_cast<uint8_t>(fields.amount)};
  return crc16Ccitt(bytes, sizeof(bytes));
}

uint16_t calculateStateChecksum(const PersistentState& value) {
  return crc16Ccitt(reinterpret_cast<const uint8_t*>(&value),
                   offsetof(PersistentState, checksum));
}

bool parseToken20(const String& input, uint64_t& value) {
  uint8_t digitCount = 0;
  value = 0;

  for (size_t i = 0; i < input.length(); ++i) {
    const char ch = input[i];
    if (ch == '-' || ch == ' ' || ch == '\t') {
      continue;
    }
    if (ch < '0' || ch > '9' || digitCount == 20) {
      return false;
    }

    const uint8_t digit = static_cast<uint8_t>(ch - '0');
    if (value > (UINT64_MAX - digit) / 10ULL) {
      return false;
    }
    value = value * 10ULL + digit;
    ++digitCount;
  }

  return digitCount == 20;
}

TokenFields unpackPlaintext(uint64_t plaintext) {
  TokenFields fields{};
  fields.tokenClass = TOKEN_CLASS_CREDIT;  // Leading carrier bits are 00.
  fields.subclass = static_cast<uint8_t>((plaintext >> 60) & 0x0fU);
  fields.random = static_cast<uint8_t>((plaintext >> 56) & 0x0fU);
  fields.tid = static_cast<uint32_t>((plaintext >> 32) & MAX_TID);
  fields.amount = static_cast<uint16_t>((plaintext >> 16) & 0xffffU);
  fields.crc = static_cast<uint16_t>(plaintext & 0xffffU);
  return fields;
}

bool loadState() {
  storageReady = InternalFS.begin();
  if (!storageReady) {
    return false;
  }

  File file(InternalFS);
  if (!file.open(STATE_FILE, FILE_O_READ)) {
    return true;  // First boot: the zero state is valid.
  }

  PersistentState loaded{};
  const int bytesRead = file.read(&loaded, sizeof(loaded));
  file.close();
  if (bytesRead != static_cast<int>(sizeof(loaded)) ||
      loaded.magic != STATE_MAGIC || loaded.version != STATE_VERSION ||
      loaded.checksum != calculateStateChecksum(loaded)) {
    Serial.println("PERINGATAN: state flash rusak; token dinonaktifkan demi keamanan.");
    storageReady = false;
    return false;
  }

  state = loaded;
  return true;
}

bool saveState(const PersistentState& value) {
  if (!storageReady) {
    return false;
  }

  InternalFS.remove(STATE_TEMP_FILE);
  File file(InternalFS);
  if (!file.open(STATE_TEMP_FILE, FILE_O_WRITE)) {
    return false;
  }
  const size_t bytesWritten = file.write(
      reinterpret_cast<const uint8_t*>(&value), sizeof(value));
  file.flush();
  file.close();
  if (bytesWritten != sizeof(value)) {
    InternalFS.remove(STATE_TEMP_FILE);
    return false;
  }

  // LittleFS rename replaces an existing file atomically. Keeping the old
  // file until this point prevents a power loss from erasing valid state.
  if (!InternalFS.rename(STATE_TEMP_FILE, STATE_FILE)) {
    InternalFS.remove(STATE_TEMP_FILE);
    return false;
  }
  return true;
}

void printUint64(uint64_t value) {
  // Arduino nRF52 Print only supports unsigned long (32-bit), while its
  // reduced printf also omits the %llu formatter. Convert 64-bit values here.
  char digits[21];
  uint8_t length = 0;
  do {
    digits[length++] = static_cast<char>('0' + (value % 10ULL));
    value /= 10ULL;
  } while (value > 0);

  while (length > 0) {
    Serial.write(static_cast<uint8_t>(digits[--length]));
  }
}

void printBalance() {
  const uint64_t whole = state.balanceTenthsKwh / 10ULL;
  const uint8_t decimal = state.balanceTenthsKwh % 10ULL;
  Serial.print("Saldo          : ");
  printUint64(whole);
  Serial.print('.');
  Serial.print(decimal);
  Serial.println(" kWh");
}

bool misty1SelfTest() {
  constexpr uint64_t plain = 0x0123456789abcdefULL;
  constexpr uint64_t encrypted = 0x8b1da5f56ab3d07cULL;
  const Misty1 cipher(DEMO_MISTY1_KEY);
  return cipher.encryptBlock(plain) == encrypted &&
         cipher.decryptBlock(encrypted) == plain;
}

bool processToken(const String& tokenText) {
  uint64_t ciphertext = 0;
  if (!parseToken20(tokenText, ciphertext)) {
    Serial.println("Gagal: token harus tepat 20 digit (tanda '-' atau spasi boleh).");
    return false;
  }

  const Misty1 cipher(DEMO_MISTY1_KEY);
  const TokenFields fields = unpackPlaintext(cipher.decryptBlock(ciphertext));

  if (fields.subclass != TOKEN_SUBCLASS_ELECTRICITY) {
    Serial.println("Gagal: token bukan kredit listrik Class 0 / Subclass 0.");
    return false;
  }
  if (fields.amount == 0) {
    Serial.println("Gagal: nilai kredit token adalah nol.");
    return false;
  }
  if (fields.crc != calculateTokenCrc(fields)) {
    Serial.println("Gagal: CRC tidak cocok; token atau key tidak valid.");
    return false;
  }
  if (!storageReady) {
    Serial.println("Gagal: flash state tidak tersedia; token tidak dipakai.");
    return false;
  }
  if (fields.tid <= state.lastUsedTid) {
    Serial.println("Gagal: TID token sudah pernah dipakai atau lebih lama.");
    return false;
  }
  if (UINT64_MAX - state.balanceTenthsKwh < fields.amount) {
    Serial.println("Gagal: saldo melampaui kapasitas penyimpanan.");
    return false;
  }

  PersistentState next = state;
  next.lastUsedTid = fields.tid;
  next.balanceTenthsKwh += fields.amount;
  next.checksum = calculateStateChecksum(next);
  if (!saveState(next)) {
    Serial.println("Gagal: state tidak dapat disimpan; saldo tidak diubah.");
    return false;
  }
  state = next;

  Serial.println("Sukses: token demo valid dan kredit ditambahkan.");
  Serial.printf("TID            : %lu\n", static_cast<unsigned long>(fields.tid));
  Serial.printf("RND            : %u\n", fields.random);
  Serial.printf("Kredit         : %u.%u kWh\n", fields.amount / 10,
                fields.amount % 10);
  printBalance();
  return true;
}

void processCommand(String command) {
  command.trim();
  if (command.equalsIgnoreCase("STATUS")) {
    Serial.printf("TID terakhir   : %lu\n",
                  static_cast<unsigned long>(state.lastUsedTid));
    printBalance();
    return;
  }
  if (command.length() > 0) {
    processToken(command);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("=== nRF52840 STS EDITION 2 EDUCATIONAL VALIDATOR ===");
  Serial.println("Demo ini bukan meter STS tersertifikasi.");

  if (!misty1SelfTest()) {
    Serial.println("FATAL: MISTY1 RFC 2994 self-test gagal.");
    return;
  }
  Serial.println("MISTY1 encrypt/decrypt self-test: OK");

  if (loadState()) {
    Serial.println("State flash: OK");
    printBalance();
  } else {
    Serial.println("State flash: GAGAL");
  }
  Serial.println("Masukkan token 20 digit, atau ketik STATUS.");
}

void loop() {
  if (Serial.available() > 0) {
    const String command = Serial.readStringUntil('\n');
    processCommand(command);
  }
}
