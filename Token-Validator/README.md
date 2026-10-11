# nRF52840 STS Edition 2 Educational Token Validator

Firmware ini memvalidasi token yang dibuat oleh proyek pendamping
`Token-Generator`. Target perangkatnya adalah nRF52840 Pro Micro/SuperMini
dengan framework Arduino dan USB CDC Serial.

> **Penting:** program ini hanya demonstrasi pembelajaran. Program ini bukan
> meter atau decoder STS tersertifikasi dan tokennya tidak dapat digunakan
> pada meter listrik nyata.

## Kesesuaian dengan generator

Validator memakai format demo yang sama dengan generator:

- token kredit listrik Class 0 / Subclass 0;
- blok 64-bit MISTY1 dan demo key 128-bit yang sama;
- payload `Subclass(4) | RND(4) | TID(24) | Amount(16) | CRC(16)`;
- TID dalam menit sejak `2014-01-01 00:00:00 UTC`;
- amount dalam unit 0,1 kWh;
- CRC-16/CCITT-FALSE versi demo.

Implementasi ini bukan implementasi normatif IEC 62055-41. Demo key berada di
source code, CRC dan amount coding disederhanakan, dan tidak ada DKGA04/KMC,
SGC, TI, KT, KRN, DRN, KEN, HSM, maupun test vector sertifikasi.

## Validasi yang dilakukan

1. Input harus berisi tepat 20 digit. Format `0000-0000-0000-0000-0000`
   maupun digit dengan spasi juga diterima.
2. Nilai desimal diperiksa agar tidak melampaui kapasitas blok 64-bit.
3. Token didekripsi dengan MISTY1.
4. Subclass, amount, dan CRC diverifikasi.
5. TID harus lebih besar daripada TID token terakhir yang berhasil dipakai.
6. TID terakhir dan saldo disimpan ke internal flash. Jika penyimpanan gagal,
   token ditolak dan saldo tidak diubah.

Konsekuensi aturan TID tersebut: hanya satu token per nilai menit TID yang
dapat digunakan. Buat token berikutnya dengan Unix time pada menit yang lebih
baru.

## Build dan upload

```bash
pio run
pio run --target upload
```

Build menghasilkan:

```text
.pio/build/nrf52840_pro_micro/firmware.bin
.pio/build/nrf52840_pro_micro/firmware.uf2
```

Jika upload otomatis tidak menemukan bootloader, tekan Reset dua kali dengan
cepat lalu ulangi upload. File UF2 juga dapat disalin manual ke drive
bootloader.

## Pemakaian

1. Upload firmware dan buka Serial Monitor pada 115200 baud:

   ```bash
   pio device monitor --baud 115200
   ```

2. Atur line ending ke newline.
3. Tempel token dari `Token-Generator`, misalnya:

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

4. Ketik `STATUS` untuk melihat saldo dan TID terakhir tanpa mengubah state.

Saat boot, firmware menjalankan self-test enkripsi dan dekripsi MISTY1 memakai
test vector RFC 2994. Saldo ditampilkan sebagai kWh dengan satu angka desimal.

## Output validasi token

### Token berhasil

Jika format token, hasil dekripsi, CRC, subclass, amount, dan TID semuanya
valid, sistem menyimpan TID serta saldo baru ke internal flash. Contoh output:

```text
Sukses: token demo valid dan kredit ditambahkan.
TID            : 6311521
RND            : 14
Kredit         : 25.0 kWh
Saldo          : 50.0 kWh
```

Keterangan:

- `TID` adalah nomor menit token sejak `2014-01-01 00:00:00 UTC`.
- `RND` adalah nilai random empat bit yang dibuat oleh generator.
- `Kredit` adalah energi yang dibawa token tersebut.
- `Saldo` adalah jumlah kredit setelah token berhasil dipakai.

### Token gagal

Token yang gagal tidak menambah saldo dan tidak mengganti TID terakhir. Sistem
menampilkan salah satu pesan berikut sesuai penyebabnya.

Input bukan tepat 20 digit atau nilainya melebihi blok 64-bit:

```text
Gagal: token harus tepat 20 digit (tanda '-' atau spasi boleh).
```

Token bukan token kredit listrik Class 0 / Subclass 0:

```text
Gagal: token bukan kredit listrik Class 0 / Subclass 0.
```

Nilai kredit di dalam token adalah nol:

```text
Gagal: nilai kredit token adalah nol.
```

Token rusak, dibuat dengan key berbeda, atau CRC tidak cocok:

```text
Gagal: CRC tidak cocok; token atau key tidak valid.
```

Token pernah digunakan, memiliki TID yang sama, atau lebih lama daripada token
terakhir:

```text
Gagal: TID token sudah pernah dipakai atau lebih lama.
```

TID memiliki resolusi satu menit. Karena itu, token untuk `07:00:00` dan
`07:00:10` mempunyai TID yang sama. Setelah salah satunya berhasil dipakai,
token berikutnya harus dibuat paling cepat untuk `07:01:00`.

Internal flash tidak tersedia:

```text
Gagal: flash state tidak tersedia; token tidak dipakai.
```

TID dan saldo baru tidak berhasil disimpan:

```text
Gagal: state tidak dapat disimpan; saldo tidak diubah.
```

Kapasitas penghitung saldo terlampaui:

```text
Gagal: saldo melampaui kapasitas penyimpanan.
```

### Perintah STATUS

Perintah `STATUS` menampilkan state saat ini tanpa memakai token atau mengubah
saldo:

```text
TID terakhir   : 6311521
Saldo          : 50.0 kWh
```

## Berkas utama

- `src/main.cpp` — parsing token, validasi, anti-replay, saldo, dan flash state.
- `src/misty1.h` / `src/misty1.cpp` — implementasi MISTY1 64-bit.
- `platformio.ini` — konfigurasi build/upload nRF52840.

## Catatan keamanan demo

State flash sengaja tidak menyediakan perintah reset melalui Serial karena
reset akan membuka kembali peluang pemakaian token lama. Untuk pengujian dari
state kosong, hapus/format internal filesystem atau flash ulang seluruh memori
perangkat dengan cara yang sesuai dengan bootloader Anda.
