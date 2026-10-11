Import("env")
import os
import subprocess

def make_uf2(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    bin_path = os.path.join(build_dir, "firmware.bin")
    uf2_path = os.path.join(build_dir, "firmware.uf2")

    if os.path.exists(bin_path):
        print("[UF2] Generating UF2 file...")
        cmd = [
            "python", "scripts/uf2conv.py",
            bin_path,
            "-c",
            "-b", "0x26000",
            "-f", "0xada52840",
            "-o", uf2_path
        ]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0 and os.path.exists(uf2_path):
            rel_path = os.path.relpath(uf2_path)
            print(f"[UF2] Build Done: {rel_path}")
        else:
            print(f"[UF2] Build Failed: {res.stderr.strip()}")

# String kosong ("") mencegah SCons mencetak 'make_uf2(...)'
env.AddPostAction("buildprog", env.Action(make_uf2, ""))