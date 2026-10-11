import sys
import os
import time
import subprocess
import serial
import serial.tools.list_ports

def print_debug_ports():
    ports = serial.tools.list_ports.comports()
    print("[DFU] Available COM ports:", flush=True)
    if not ports:
        print("      (No COM ports detected)", flush=True)
    for p in ports:
        vid_str = f"0x{p.vid:04X}" if p.vid else "None"
        pid_str = f"0x{p.pid:04X}" if p.pid else "None"
        print(f"      - {p.device}: {p.description} [VID:{vid_str} PID:{pid_str}]", flush=True)

def is_bootloader_port(port):
    desc = (port.description or "").lower()
    prod = (port.product or "").lower()
    mfg = (port.manufacturer or "").lower()
    
    if port.pid == 0xCAFE:
        return False

    keywords = ["bootloader", "uf2 bootloader", "adafruit nrf52 bootloader"]
    for kw in keywords:
        if kw in desc or kw in prod or kw in mfg:
            return True
            
    bootloader_pids = [0x0029, 0x002A, 0x00B0, 0x002B, 0x8029, 0x802A]
    if port.vid == 0x239A and port.pid in bootloader_pids:
        return True
        
    return False

def find_bootloader_port():
    for port in serial.tools.list_ports.comports():
        if is_bootloader_port(port):
            return port.device
    return None

def prepare_bootloader_port():
    print_debug_ports()

    # 1. Cek apakah board terdeteksi sebagai bootloader
    existing_boot_port = find_bootloader_port()
    if existing_boot_port:
        print(f"[DFU] Board explicitly detected in bootloader mode on {existing_boot_port}.", flush=True)
        return existing_boot_port

    # Catat port yang tersedia sebelum reset
    initial_ports = [p.device for p in serial.tools.list_ports.comports()]

    # 2. Kirim sinyal 1200 baud reset
    for port in initial_ports:
        try:
            print(f"[DFU] Sending 1200 baud reset signal to {port}...", flush=True)
            ser = serial.Serial(port, 1200)
            ser.close()
        except Exception:
            pass

    # 3. Tunggu sebentar untuk melihat apakah ada port baru / port bootloader muncul
    time.sleep(1.5)
    start_time = time.time()
    while time.time() - start_time < 3.0:
        boot_port = find_bootloader_port()
        if boot_port:
            return boot_port

        current_ports = [p.device for p in serial.tools.list_ports.comports()]
        new_ports = [p for p in current_ports if p not in initial_ports]
        if new_ports:
            print(f"[DFU] New port detected post-reset: {new_ports[0]}", flush=True)
            return new_ports[0]

        time.sleep(0.5)

    # 4. FALLBACK STRATEGY: Asumsikan port yang ada saat ini sebagai target bootloader
    current_ports = [p.device for p in serial.tools.list_ports.comports()]
    fallback_port = current_ports[0] if current_ports else (initial_ports[0] if initial_ports else None)

    if fallback_port:
        print(f"[DFU] Assuming target port {fallback_port} is in bootloader mode.", flush=True)
        return fallback_port

    return None

def get_nrfutil_executable():
    python_dir = os.path.dirname(sys.executable)
    nrfutil_exe = os.path.join(python_dir, "adafruit-nrfutil.exe")
    if os.path.exists(nrfutil_exe):
        return nrfutil_exe
    return "adafruit-nrfutil"

def get_latest_firmware(build_dir):
    hex_path = os.path.join(build_dir, "firmware.hex")
    bin_path = os.path.join(build_dir, "firmware.bin")

    if os.path.exists(hex_path):
        return hex_path
    elif os.path.exists(bin_path):
        return bin_path
    return None

def upload():
    build_dir = sys.argv[1]
    input_path = get_latest_firmware(build_dir)
    zip_pkg_path = os.path.join(build_dir, "firmware.zip")

    if not input_path or not os.path.exists(input_path):
        print("[Error] No firmware file found (.hex or .bin).", flush=True)
        sys.exit(1)

    rel_input_path = os.path.relpath(input_path)
    rel_zip_path = os.path.relpath(zip_pkg_path)
    nrfutil_bin = get_nrfutil_executable()

    print(f"[DFU] Using firmware: {rel_input_path}", flush=True)

    # 1. Tentukan target port
    print("[DFU] [1/3] Checking board status...", flush=True)
    target_port = prepare_bootloader_port()
    if not target_port:
        print("[Error] No active COM port available to flash.", flush=True)
        sys.exit(1)

    print(f"[DFU] Target port: {target_port}", flush=True)

    # 2. Generate paket DFU
    print(f"[DFU] [2/3] Generating package ({rel_zip_path})...", flush=True)
    gen_cmd = [
        nrfutil_bin, "dfu", "genpkg",
        "--dev-type", "0x0052",
        "--application", input_path,
        zip_pkg_path
    ]

    use_shell = not os.path.isabs(nrfutil_bin)
    res = subprocess.run(gen_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=use_shell)

    if res.returncode != 0:
        print(f"[Error] Failed to generate DFU package:\n{res.stderr.strip()}", flush=True)
        sys.exit(1)

    # 3. Flashing paket DFU (jika gagal di sini, baru tampilkan error)
    print(f"[DFU] [3/3] Flashing firmware to {target_port}...", flush=True)
    flash_cmd = [
        nrfutil_bin, "dfu", "serial",
        "-p", target_port,
        "-b", "115200",
        "-pkg", zip_pkg_path
    ]
    res = subprocess.run(flash_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, shell=use_shell)

    if res.returncode != 0:
        print(f"[Error] Flashing failed on {target_port}.\n{res.stderr.strip()}", flush=True)
        sys.exit(1)

    print("[DFU] Success! Upload complete, board is restarting.", flush=True)

if __name__ == "__main__":
    upload()