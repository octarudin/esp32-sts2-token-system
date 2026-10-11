import sys
import os
import string
import time
import subprocess

def print_progress(copied, total, bar_length=30):
    percent = float(copied) / total
    filled_length = int(round(bar_length * percent))
    bar = '=' * filled_length + '-' * (bar_length - filled_length)
    sys.stdout.write(f"\r[UF2] Writing: [{bar}] {int(percent * 100)}% ({copied}/{total} bytes)")
    sys.stdout.flush()

def upload():
    build_dir = sys.argv[1]
    bin_path = os.path.join(build_dir, "firmware.bin")
    uf2_path = os.path.join(build_dir, "firmware.uf2")

    if not os.path.exists(bin_path):
        print(f"[Error] Binary file not found: {bin_path}")
        sys.exit(1)

    # 1. Konversi biner ke UF2 secara senyap
    cmd = [
        "python", "scripts/uf2conv.py",
        bin_path,
        "-c",
        "-b", "0x26000",
        "-f", "0xada52840",
        "-o", uf2_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0 or not os.path.exists(uf2_path):
        print(f"[Error] Conversion failed: {res.stderr.strip()}")
        sys.exit(1)

    # 2. Cari Drive Bootloader UF2
    target_drive = None
    for letter in string.ascii_uppercase:
        drive = f"{letter}:\\"
        info_file = os.path.join(drive, "INFO_UF2.TXT")
        if os.path.exists(info_file):
            target_drive = drive
            break

    if not target_drive:
        print("[Error] Bootloader drive not found. Please double-tap Reset.")
        sys.exit(1)

    # 3. Flashing dengan Progress Bar
    print(f"[UF2] Target drive found: {target_drive}")
    dst_file = os.path.join(target_drive, "firmware.uf2")
    
    total_size = os.path.getsize(uf2_path)
    copied_size = 0
    chunk_size = 4096

    try:
        with open(uf2_path, 'rb') as fsrc, open(dst_file, 'wb') as fdst:
            while True:
                buf = fsrc.read(chunk_size)
                if not buf:
                    break
                fdst.write(buf)
                fdst.flush()
                copied_size += len(buf)
                print_progress(copied_size, total_size)
                time.sleep(0.005)
    except OSError:
        pass

    print_progress(total_size, total_size)
    print("\n[UF2] Upload complete. Exiting bootloader mode and running application...")

if __name__ == "__main__":
    upload()