# ESP32 STS Edition 2 Educational Token Generator

Program ini adalah demonstrasi pembelajaran bentuk dasar token STS Edition 2.
Program **bukan** vending system STS yang tersertifikasi dan token keluarannya
tidak boleh digunakan pada meter listrik nyata.

## Bagian yang didemonstrasikan

- Token kredit listrik Class 0 / Subclass 0.
- Bentuk field publik `Class(2) | Subclass(4) | RND(4) | TID(24) |
  Amount(16) | CRC(16)`.
- TID sebagai jumlah menit sejak base date `2014-01-01 00:00:00 UTC`.
- MISTY1 dengan key 128-bit dan blok 64-bit, berdasarkan RFC 2994.
- Representasi token sebagai 20 digit dalam lima kelompok.
- Self-test MISTY1 saat boot menggunakan test vector RFC 2994.

## Penyederhanaan penting

- `DEMO_MISTY1_KEY` adalah key contoh bersama, bukan decoder key hasil DKGA04.
- CRC memakai CRC-16/CCITT-FALSE versi demo, bukan aturan CRC IEC 62055-41.
- Amount diperlakukan langsung dalam unit 0,1 kWh; coding transfer amount resmi
  IEC 62055-41 belum diterapkan.
- Program belum memakai KMC, SGC, TI, KT, KRN, DRN, KEN, HSM/security module,
  ataupun test vector sertifikasi STS.
- Penyusunan carrier 66-bit pada demo dibatasi ke Class 0, sehingga dua bit
  Class bernilai `00` dan ciphertext MISTY1 dapat diformat dari `uint64_t`.

## Build

```bash
pio run
```

## Pemakaian

1. Upload firmware ke ESP32.
2. Buka Serial Monitor dengan baud `115200` dan line ending newline.
3. Masukkan:

   ```text
   amount_tenths_kWh,waktu
   ```

   Kolom `waktu` dapat berupa Unix timestamp atau tanggal lokal GMT+7/WIB
   dengan format `YYYY-MM-DD HH:MM:SS`.

   Contoh 25,0 kWh pada `2026-01-01 00:00:00 UTC`:

   ```text
   250,1767225600
   ```

### Contoh GMT+7 (WIB/Jakarta)

Unix timestamp selalu merepresentasikan waktu absolut berbasis UTC. Karena
WIB adalah UTC+7, waktu berikut menunjukkan saat yang sama:

```text
2026-01-01 07:00:00 GMT+7 (WIB)
2026-01-01 00:00:00 UTC
Unix timestamp: 1767225600
```

Jika pengguna memasukkan tanggal GMT+7/WIB, program terlebih dahulu
mengonversinya menjadi Unix timestamp berbasis UTC. Unix timestamp hasil
konversi inilah yang digunakan untuk menghitung TID; offset GMT+7 tidak
disimpan sebagai bagian dari token.

Untuk membuat token 25,0 kWh pada `1 Januari 2026 pukul 07:00 WIB`, masukkan:

```text
250,2026-01-01 07:00:00
```

Input Unix timestamp yang setara tetap didukung:

```text
250,1767225600
```

Contoh lain, `1 Januari 2026 pukul 00:00 WIB` sama dengan
`31 Desember 2025 pukul 17:00 UTC`, sehingga inputnya:

```text
250,2026-01-01 00:00:00
```

Setiap permintaan menggunakan RND empat bit dari hardware random generator
ESP32, sehingga token untuk input yang sama dapat berbeda.

### Resolusi waktu TID

TID dihitung sebagai jumlah **menit penuh** sejak base date
`2014-01-01 00:00:00 UTC`. Perubahan detik dalam menit yang sama tidak
mengubah TID. Sebagai contoh, kedua input berikut menghasilkan TID yang sama:

```text
250,2026-01-01 07:00:00
250,2026-01-01 07:00:59
```

TID baru berubah ketika waktu berpindah ke menit berikutnya, misalnya pada
`2026-01-01 07:01:00`. Token akhirnya masih dapat berbeda meskipun TID sama
karena setiap permintaan menggunakan nilai RND acak.

## Output program

### Berhasil membuat token

Untuk input berikut:

```text
250,2026-01-01 07:00:00
```

Serial Monitor menampilkan output dengan bentuk berikut:

```text
--- EDUCATIONAL STS EDITION 2 TOKEN ---
Class/Subclass : 0 / 0 (electricity credit)
RND            : <nilai acak 0..15>
TID            : 6311520 minutes since 2014-01-01 UTC
Amount field   : 250 (25.0 kWh in this demo)
Demo CRC       : <nilai CRC>
Token          : <20 digit token dalam lima kelompok>
NOT FOR REAL METERS / NOT STS CERTIFIED
```

Nilai RND, CRC, dan token pada contoh di atas dapat berubah pada setiap
permintaan.

### Gagal membuat token

Jika nilai amount atau format tanggal tidak valid, misalnya:

```text
250,2026-02-30 07:00:00
```

outputnya adalah:

```text
Nilai tidak valid. Gunakan amount 1..65535 dan waktu berupa
Unix time atau YYYY-MM-DD HH:MM:SS dalam GMT+7/WIB.
```

Jika tanggal valid tetapi berada di luar rentang TID yang didukung, program
menampilkan:

```text
Gagal: waktu di luar rentang TID base date 2014 atau amount nol.
```

Jika struktur perintah salah, misalnya tidak memiliki pemisah koma, program
menampilkan:

```text
Format salah. Gunakan: amount_tenths_kWh,waktu
Contoh: 250,2026-01-01 07:00:00
```

## Referensi

- IEC 62055-41 untuk implementasi STS normatif.
- STS Association, penjelasan TID dan struktur field token 66-bit.
- RFC 2994, deskripsi dan test vector MISTY1.

