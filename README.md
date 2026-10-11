# ESP32 STS Edition 2 Educational Token System

Repositori ini berisi demonstrasi end-to-end sistem token prabayar bergaya
**Standard Transfer Specification (STS) Edition 2**. Sistem terdiri dari dua
firmware:

- [`Token-Generator`](./Token-Generator) — berjalan pada ESP32 untuk membuat
  token kredit listrik 20 digit.
- [`Token-Validator`](./Token-Validator) — berjalan pada nRF52840 Pro
  Micro/SuperMini untuk memvalidasi token, mencegah penggunaan ulang, dan
  menyimpan saldo ke internal flash.

> [!WARNING]
> Proyek ini hanya untuk pembelajaran dan prototipe. Implementasi ini **bukan
> vending system atau meter STS tersertifikasi**, belum sepenuhnya mengikuti
> IEC 62055-41, dan tokennya tidak dapat digunakan pada meter listrik nyata.

## Tujuan repositori

Repositori ini dibuat untuk memperlihatkan alur dasar sistem token offline:

1. generator menerima nilai kredit dan waktu;
2. data token disusun, diberi CRC, lalu dienkripsi;
3. hasil enkripsi direpresentasikan sebagai token desimal 20 digit;
4. validator menerima dan mendekripsi token;
5. validator memeriksa format, tipe token, CRC, nilai kredit, dan TID;
6. token yang valid menambah saldo, sedangkan token lama atau yang pernah
   dipakai ditolak.

Dengan demikian, proyek ini dapat digunakan untuk mempelajari enkripsi blok,
integritas data, representasi token numerik, pencatatan saldo persisten, dan
mekanisme anti-replay tanpa koneksi internet saat token digunakan.

## Cara kerja

Format logis yang didemonstrasikan adalah:

```text
Class(2) | Subclass(4) | RND(4) | TID(24) | Amount(16) | CRC(16)
```

Khusus demo Class 0 ini, dua bit Class menjadi carrier `00`. Blok 64-bit yang
dienkripsi berisi:

```text
Subclass(4) | RND(4) | TID(24) | Amount(16) | CRC(16)
```

- **Class/Subclass 0/0** menandai token kredit listrik pada demo.
- **RND** adalah angka acak 4-bit dari hardware random generator ESP32.
- **TID** adalah jumlah menit penuh sejak `2014-01-01 00:00:00 UTC`.
- **Amount** adalah kredit dalam satuan 0,1 kWh, dengan rentang input
  `1..65535` atau 0,1 sampai 6.553,5 kWh.
- **CRC** adalah CRC-16/CCITT-FALSE versi demo.
- Payload dienkripsi menggunakan **MISTY1**, blok 64-bit dengan key 128-bit.

Generator dan validator memakai demo key yang sama. Validator menyimpan
`lastUsedTid` dan saldo dalam internal flash. Sebuah token hanya diterima jika
TID-nya lebih besar daripada TID terakhir yang berhasil digunakan.

## Peralatan yang dibutuhkan

### Hardware

- 1 board ESP32 DevKit yang kompatibel dengan board PlatformIO `esp32dev`;
- 1 board nRF52840 Pro Micro/SuperMini;
- 2 kabel USB yang mendukung transfer data;
- komputer dengan port USB yang tersedia.

Tidak ada sambungan data langsung antara kedua board. Token dipindahkan dari
Serial Monitor generator ke Serial Monitor validator.

### Software

- [Git](https://git-scm.com/);
- [Visual Studio Code](https://code.visualstudio.com/) dengan ekstensi
  PlatformIO IDE, atau PlatformIO Core melalui perintah `pio`;
- driver USB/serial yang sesuai dengan board;
- Python 3 untuk script upload UF2 validator.

PlatformIO akan mengambil platform dan framework yang diperlukan dari
`platformio.ini`: Arduino-ESP32 untuk generator dan Arduino nRF52 untuk
validator.

## Cara menggunakan repositori

### 1. Ambil source code

```bash
git clone <URL_REPOSITORI>
cd esp32-sts2-token-system
```

### 2. Build dan upload generator

Hubungkan ESP32, lalu jalankan:

```bash
cd Token-Generator
pio run
pio run --target upload
pio device monitor --baud 115200
```

Atur line ending Serial Monitor ke **newline**. Format input generator adalah:

```text
amount_tenths_kWh,waktu
```

`waktu` dapat berupa Unix timestamp atau `YYYY-MM-DD HH:MM:SS`. Input tanggal
dianggap menggunakan GMT+7/WIB, lalu dikonversi ke UTC untuk menghitung TID.

Contoh untuk kredit 25,0 kWh pada 1 Januari 2026 pukul 07:00 WIB:

```text
250,2026-01-01 07:00:00
```

Input Unix timestamp yang setara:

```text
250,1767225600
```

### 3. Build dan upload validator

Buka terminal baru dari root repositori, hubungkan nRF52840, lalu jalankan:

```bash
cd Token-Validator
pio run
pio run --target upload
pio device monitor --baud 115200
```

Build validator juga menghasilkan file berikut untuk upload UF2 manual:

```text
.pio/build/nrf52840_pro_micro/firmware.uf2
```

Jika bootloader tidak terdeteksi, tekan tombol Reset dua kali dengan cepat,
lalu ulangi upload atau salin `firmware.uf2` ke drive bootloader yang muncul.

### 4. Validasi token

Salin hanya nilai pada baris `Token` dari output generator, lalu tempel ke
Serial Monitor validator. Validator menerima 20 digit tanpa pemisah, dengan
tanda hubung, atau dengan spasi, misalnya:

```text
1106-4855-4771-7937-6256
```

Untuk melihat saldo dan TID terakhir tanpa mengubah state, kirim:

```text
STATUS
```

## Simulasi generate dan validate token

Contoh alur berikut menggunakan kredit 25,0 kWh. Nilai RND, CRC, dan token
dapat berbeda pada setiap generate meskipun amount dan waktunya sama.

### A. Generate

Kirim ke ESP32:

```text
250,2026-01-01 07:00:00
```

Contoh output:

```text
--- EDUCATIONAL STS EDITION 2 TOKEN ---
Class/Subclass : 0 / 0 (electricity credit)
RND            : 14
TID            : 6311520 minutes since 2014-01-01 UTC
Amount field   : 250 (25.0 kWh in this demo)
Demo CRC       : 0xB1B9
Token          : 1106-4855-4771-7937-6256
NOT FOR REAL METERS / NOT STS CERTIFIED
```

### B. Validate

Kirim token ke nRF52840:

```text
1106-4855-4771-7937-6256
```

Validator akan menampilkan keluaran seperti:

```text
Sukses: token demo valid dan kredit ditambahkan.
TID            : 6311520
RND            : 14
Kredit         : 25.0 kWh
Saldo          : 25.0 kWh
```

Jika token yang sama dikirim lagi, validator menolaknya:

```text
Gagal: TID token sudah pernah dipakai atau lebih lama.
```

Untuk membuat token berikutnya, gunakan waktu pada menit yang lebih baru,
misalnya `2026-01-01 07:01:00`. TID hanya memiliki resolusi satu menit;
`07:00:00` dan `07:00:59` menghasilkan TID yang sama dan hanya salah satunya
dapat dipakai pada validator yang sama.

## Mengapa menggunakan STS Edition 2?

Untuk sistem prabayar berbasis keypad, STS menyediakan pola pertukaran token
yang ringkas dan dapat divalidasi secara offline. Pengguna cukup memasukkan
token numerik; perangkat penerima tidak harus terhubung ke server pada saat
token digunakan. Standardisasi juga dibutuhkan agar vending system, proses
pengelolaan key, dan perangkat penerima memiliki aturan interoperabilitas dan
keamanan yang sama.

Edition 2 relevan karena menggunakan base date TID 2014 dan menjadi jalur
migrasi dari siklus TID lama Edition 1. Namun, penggunaan nama dan konsep
STS2 pada repositori ini tidak membuat implementasinya otomatis patuh STS.
Untuk produk nyata, implementasi harus mengikuti dokumen IEC 62055-41,
prosedur STS Association, key management resmi, pengujian interoperabilitas,
dan sertifikasi yang berlaku.

## Kelebihan dan kekurangan program

### Kelebihan

- alur generator-validator lengkap dan relatif mudah dipelajari;
- token dapat dipindahkan secara offline dalam format 20 digit;
- terdapat self-test MISTY1 dengan test vector RFC 2994 saat boot;
- RND generator berasal dari hardware random generator ESP32;
- validator memeriksa CRC, subclass, amount, dan urutan TID;
- saldo dan TID terakhir persisten di internal flash;
- penyimpanan validator memakai file sementara dan rename agar pembaruan
  state lebih tahan terhadap kehilangan daya;
- source code kecil dan mudah dimodifikasi untuk eksperimen.

### Kekurangan

- bukan implementasi IEC 62055-41 yang lengkap atau tersertifikasi;
- demo key yang sama ditanam langsung di source code kedua perangkat;
- CRC dan encoding amount disederhanakan dan tidak mengikuti aturan resmi;
- belum ada KMC, SGC, TI, KT, KRN, DRN, KEN, DKGA04, HSM, atau rotasi key;
- aturan anti-replay hanya menyimpan TID terbesar, sehingga dua token valid
  dalam menit yang sama tidak dapat digunakan pada validator yang sama;
- tidak ada identitas unik meter pada proses penerbitan token;
- tidak ada autentikasi pengguna, audit log, backend vending, pembatalan,
  rekonsiliasi, ataupun pemulihan state;
- keamanan fisik, secure boot, flash encryption, dan proteksi debug belum
  diterapkan;
- ketahanan flash dan skenario brownout/power-loss belum diuji sebagai produk.

## Contoh bidang penerapan

Pola token offline seperti ini dapat dipelajari atau dikembangkan lebih lanjut
untuk:

- meter listrik prabayar;
- distribusi air atau gas prabayar;
- kredit waktu pada mesin, kios, atau perangkat sewa;
- voucher akses fasilitas, parkir, loker, atau charging station;
- distribusi kuota pada perangkat yang konektivitasnya terbatas;
- laboratorium pendidikan untuk kriptografi embedded dan anti-replay;
- proof-of-concept voucher offline pada daerah dengan jaringan tidak stabil.

Penerapan di luar kelistrikan biasanya bukan produk STS standar. Desain token,
regulasi, ancaman keamanan, dan mekanisme akuntansinya harus disesuaikan dengan
bidang masing-masing.

## Perubahan yang diperlukan untuk produksi

Program ini tidak boleh diproduksikan hanya dengan mengganti demo key. Minimal
diperlukan pekerjaan berikut:

1. **Kepatuhan standar:** implementasikan IEC 62055-41 secara lengkap,
   termasuk encoding transfer amount, CRC, token carrier, seluruh tipe token,
   aturan TID, dan test vector resmi.
2. **Key management:** gunakan key unik per meter yang diturunkan melalui
   mekanisme resmi seperti DKGA04/KMC; kelola SGC, TI, KT, KRN, DRN, dan KEN
   sesuai spesifikasi serta prosedur operasional.
3. **Proteksi key:** jangan menyimpan key plaintext di source code. Gunakan
   HSM pada backend dan secure element atau hardware-backed key storage pada
   perangkat; sediakan provisioning, rotasi, revokasi, backup, dan audit.
4. **Identitas dan vending backend:** ikat token ke meter yang dituju,
   autentikasi operator/pelanggan, catat transaksi secara idempoten, serta
   sediakan rekonsiliasi, refund, monitoring, dan audit log yang tahan ubah.
5. **Anti-replay yang benar:** terapkan aturan token identifier dan duplicate
   detection sesuai standar, bukan hanya membandingkan satu `lastUsedTid`.
6. **Penyimpanan andal:** gunakan skema state transaksional dengan deteksi
   korupsi, wear leveling, strategi recovery, proteksi rollback, dan pengujian
   brownout pada seluruh titik penulisan.
7. **Hardening perangkat:** aktifkan secure boot, signed firmware, flash
   encryption, proteksi debug/JTAG, anti-rollback firmware, dan mekanisme
   update firmware yang aman.
8. **Validasi input dan fault handling:** tambahkan batas kredit bisnis,
   penanganan overflow menyeluruh, watchdog, rate limiting, logging aman, dan
   perilaku fail-safe untuk setiap kegagalan hardware.
9. **Quality assurance:** tambahkan unit test, integration test, fuzzing,
   hardware-in-the-loop test, pengujian gangguan daya, static analysis,
   code review, threat modeling, dan penetration test.
10. **Sertifikasi dan operasi:** lakukan pengujian interoperabilitas serta
    sertifikasi STS yang diperlukan, lalu penuhi regulasi metrologi, keamanan
    siber, keselamatan listrik, privasi, dan operasional di negara penerapan.

## Struktur repositori

```text
esp32-sts2-token-system/
├── Token-Generator/   # Firmware generator untuk ESP32
│   ├── src/
│   └── platformio.ini
├── Token-Validator/   # Firmware validator untuk nRF52840
│   ├── src/
│   ├── scripts/
│   └── platformio.ini
└── README.md
```

Dokumentasi detail masing-masing firmware tersedia di
[`Token-Generator/README.md`](./Token-Generator/README.md) dan
[`Token-Validator/README.md`](./Token-Validator/README.md).

## Referensi teknis

- IEC 62055-41 untuk implementasi STS normatif;
- dokumentasi STS Association untuk STS Edition 2, TID, key management, dan
  proses sertifikasi;
- RFC 2994 untuk algoritma dan test vector MISTY1.
