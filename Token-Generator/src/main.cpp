#include <Arduino.h>
#include <esp_system.h>

#include "misty1.h"

// EDUCATIONAL DEMO ONLY - not an STS-certified vending implementation.
// A real implementation requires IEC 62055-41, DKGA04/KMC key material,
// the official CRC/amount coding rules, certified test vectors and testing.

namespace {

constexpr uint8_t TOKEN_CLASS_CREDIT = 0;
constexpr uint8_t TOKEN_SUBCLASS_ELECTRICITY = 0;
constexpr uint32_t MAX_TID = 0x00ffffffUL;
constexpr uint64_t UNIX_2014_01_01_UTC = 1388534400ULL;
constexpr uint32_t WIB_OFFSET_SECONDS = 7UL * 60UL * 60UL;

// Demo key only. Never ship a shared key in firmware.
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

uint16_t demoCrc16Ccitt(const uint8_t* data, size_t length) {
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

uint16_t calculateDemoCrc(const TokenFields& fields) {
  const uint8_t bytes[] = {
      fields.tokenClass,
      fields.subclass,
      fields.random,
      static_cast<uint8_t>(fields.tid >> 16),
      static_cast<uint8_t>(fields.tid >> 8),
      static_cast<uint8_t>(fields.tid),
      static_cast<uint8_t>(fields.amount >> 8),
      static_cast<uint8_t>(fields.amount)};
  return demoCrc16Ccitt(bytes, sizeof(bytes));
}

bool tidFromUnixTime(uint64_t unixTime, uint32_t& tid) {
  if (unixTime < UNIX_2014_01_01_UTC) {
    return false;
  }
  const uint64_t minutes = (unixTime - UNIX_2014_01_01_UTC) / 60ULL;
  if (minutes > MAX_TID) {
    return false;
  }
  tid = static_cast<uint32_t>(minutes);
  return true;
}

bool isLeapYear(uint16_t year) {
  return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  constexpr uint8_t DAYS_PER_MONTH[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2U && isLeapYear(year)) {
    return 29;
  }
  return DAYS_PER_MONTH[month - 1U];
}

bool parseFixedDigits(const String& text, size_t start, size_t count,
                      uint16_t& value) {
  value = 0;
  for (size_t i = 0; i < count; ++i) {
    const char character = text.charAt(start + i);
    if (character < '0' || character > '9') {
      return false;
    }
    value = static_cast<uint16_t>(value * 10U + (character - '0'));
  }
  return true;
}

// Converts YYYY-MM-DD HH:MM:SS in GMT+7/WIB to a UTC Unix timestamp.
bool parseWibDateTime(const String& text, uint64_t& unixTime) {
  if (text.length() != 19U || text.charAt(4) != '-' ||
      text.charAt(7) != '-' || text.charAt(10) != ' ' ||
      text.charAt(13) != ':' || text.charAt(16) != ':') {
    return false;
  }

  uint16_t year = 0;
  uint16_t month = 0;
  uint16_t day = 0;
  uint16_t hour = 0;
  uint16_t minute = 0;
  uint16_t second = 0;
  if (!parseFixedDigits(text, 0, 4, year) ||
      !parseFixedDigits(text, 5, 2, month) ||
      !parseFixedDigits(text, 8, 2, day) ||
      !parseFixedDigits(text, 11, 2, hour) ||
      !parseFixedDigits(text, 14, 2, minute) ||
      !parseFixedDigits(text, 17, 2, second) || year < 1970U ||
      month < 1U || month > 12U || day < 1U ||
      day > daysInMonth(year, static_cast<uint8_t>(month)) || hour > 23U ||
      minute > 59U || second > 59U) {
    return false;
  }

  uint64_t daysSinceEpoch = 0;
  for (uint16_t currentYear = 1970U; currentYear < year; ++currentYear) {
    daysSinceEpoch += isLeapYear(currentYear) ? 366ULL : 365ULL;
  }
  for (uint8_t currentMonth = 1U; currentMonth < month; ++currentMonth) {
    daysSinceEpoch += daysInMonth(year, currentMonth);
  }
  daysSinceEpoch += day - 1U;

  const uint64_t localSeconds = daysSinceEpoch * 86400ULL + hour * 3600ULL +
                                minute * 60ULL + second;
  if (localSeconds < WIB_OFFSET_SECONDS) {
    return false;
  }
  unixTime = localSeconds - WIB_OFFSET_SECONDS;
  return true;
}

bool parseTimeInput(const String& text, uint64_t& unixTime) {
  char* timeEnd = nullptr;
  const unsigned long long numericTime = strtoull(text.c_str(), &timeEnd, 10);
  if (text.length() > 0U && *timeEnd == '\0') {
    unixTime = numericTime;
    return true;
  }
  return parseWibDateTime(text, unixTime);
}

uint64_t packPlaintext(const TokenFields& fields) {
  // The two Class bits are represented by the token carrier. For a Class 0
  // credit token they are 00. The remaining public STS-style fields fill the
  // 64-bit MISTY1 block: Subclass(4), RND(4), TID(24), Amount(16), CRC(16).
  return (static_cast<uint64_t>(fields.subclass & 0x0fU) << 60) |
         (static_cast<uint64_t>(fields.random & 0x0fU) << 56) |
         (static_cast<uint64_t>(fields.tid & MAX_TID) << 32) |
         (static_cast<uint64_t>(fields.amount) << 16) |
         fields.crc;
}

String formatToken20(uint64_t value) {
  char digits[21];
  snprintf(digits, sizeof(digits), "%020llu",
           static_cast<unsigned long long>(value));

  String formatted;
  formatted.reserve(24);
  for (uint8_t i = 0; i < 20; ++i) {
    if (i != 0 && (i % 4) == 0) {
      formatted += '-';
    }
    formatted += digits[i];
  }
  return formatted;
}

bool misty1SelfTest() {
  constexpr uint8_t key[16] = {
      0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  Misty1 cipher(key);
  return cipher.encryptBlock(0x0123456789abcdefULL) == 0x8b1da5f56ab3d07cULL;
}

bool generateEducationalToken(uint16_t amountTenthsKwh, uint64_t unixTime,
                              String& token, TokenFields& fields) {
  if (amountTenthsKwh == 0) {
    return false;
  }

  uint32_t tid = 0;
  if (!tidFromUnixTime(unixTime, tid)) {
    return false;
  }

  fields.tokenClass = TOKEN_CLASS_CREDIT;
  fields.subclass = TOKEN_SUBCLASS_ELECTRICITY;
  fields.random = static_cast<uint8_t>(esp_random() & 0x0fU);
  fields.tid = tid;
  fields.amount = amountTenthsKwh;
  fields.crc = calculateDemoCrc(fields);

  const Misty1 cipher(DEMO_MISTY1_KEY);
  const uint64_t ciphertext = cipher.encryptBlock(packPlaintext(fields));
  token = formatToken20(ciphertext);  // Class 0 has leading carrier bits 00.
  return true;
}

void printToken(const String& token, const TokenFields& fields) {
  Serial.println();
  Serial.println("--- EDUCATIONAL STS EDITION 2 TOKEN ---");
  Serial.printf("Class/Subclass : %u / %u (electricity credit)\n",
                fields.tokenClass, fields.subclass);
  Serial.printf("RND            : %u\n", fields.random);
  Serial.printf("TID            : %lu minutes since 2014-01-01 UTC\n",
                static_cast<unsigned long>(fields.tid));
  Serial.printf("Amount field   : %u (%.1f kWh in this demo)\n",
                fields.amount, fields.amount / 10.0);
  Serial.printf("Demo CRC       : 0x%04X\n", fields.crc);
  Serial.print("Token          : ");
  Serial.println(token);
  Serial.println("NOT FOR REAL METERS / NOT STS CERTIFIED");
}

void processCommand(String command) {
  command.trim();
  const int comma = command.indexOf(',');
  if (comma <= 0 || comma == static_cast<int>(command.length()) - 1) {
    Serial.println("Format salah. Gunakan: amount_tenths_kWh,waktu");
    Serial.println("Contoh: 250,2026-01-01 07:00:00");
    return;
  }

  const String amountText = command.substring(0, comma);
  const String timeText = command.substring(comma + 1);
  char* amountEnd = nullptr;
  const unsigned long amount = strtoul(amountText.c_str(), &amountEnd, 10);
  uint64_t unixTime = 0;

  if (*amountEnd != '\0' || amount == 0 || amount > 65535UL ||
      !parseTimeInput(timeText, unixTime)) {
    Serial.println("Nilai tidak valid. Gunakan amount 1..65535 dan waktu berupa");
    Serial.println("Unix time atau YYYY-MM-DD HH:MM:SS dalam GMT+7/WIB.");
    return;
  }

  String token;
  TokenFields fields{};
  if (!generateEducationalToken(static_cast<uint16_t>(amount), unixTime,
                                token, fields)) {
    Serial.println("Gagal: waktu di luar rentang TID base date 2014 atau amount nol.");
    return;
  }
  printToken(token, fields);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 STS EDITION 2 EDUCATIONAL GENERATOR ===");
  Serial.println("Demo ini bukan vending system STS tersertifikasi.");

  if (!misty1SelfTest()) {
    Serial.println("FATAL: MISTY1 RFC 2994 self-test gagal.");
    return;
  }

  Serial.println("MISTY1 RFC 2994 self-test: OK");
  Serial.println("Masukkan: amount_tenths_kWh,waktu");
  Serial.println("Waktu: Unix time atau YYYY-MM-DD HH:MM:SS (GMT+7/WIB)");
  Serial.println("Contoh 25.0 kWh: 250,2026-01-01 07:00:00");
}

void loop() {
  if (Serial.available() > 0) {
    const String command = Serial.readStringUntil('\n');
    processCommand(command);
  }
}
