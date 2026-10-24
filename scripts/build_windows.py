#!/usr/bin/env python3
"""Build Windows x64 from Linux with MinGW-w64, then package using NSIS."""
import argparse
import concurrent.futures
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain-root", type=pathlib.Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-installer", action="store_true")
    args = parser.parse_args()
    env = dict(os.environ)
    if args.toolchain_root:
        base = args.toolchain_root.resolve() / "usr"
        env["PATH"] = str(base / "bin") + os.pathsep + env.get("PATH", "")
        env["LD_LIBRARY_PATH"] = str(base / "lib/x86_64-linux-gnu") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
        env["NSISDIR"] = str(base / "share/nsis")
    def find(name):
        result = shutil.which(name, path=env["PATH"])
        if not result:
            raise RuntimeError(f"Required build tool missing: {name}")
        return result
    compiler = find("x86_64-w64-mingw32-g++-posix")
    preprocessor = find("x86_64-w64-mingw32-gcc-posix")
    windres = find("x86_64-w64-mingw32-windres")
    build = ROOT / "build-windows"
    package = build / "package"
    package.mkdir(parents=True, exist_ok=True)
    def run(command):
        subprocess.run([str(x) for x in command], cwd=ROOT, env=env, check=True)
    run([windres, "--preprocessor=" + preprocessor, "--preprocessor-arg=-E", "--preprocessor-arg=-xc", "--preprocessor-arg=-DRC_INVOKED", "-I", "packaging", "-i", "packaging/app.rc", "-o", build / "app-res.o"])
    flags = ["-std=c++20", "-O2", "-DNDEBUG", "-DUNICODE", "-D_UNICODE", "-DWINVER=0x0A00", "-D_WIN32_WINNT=0x0A00", "-Isrc", "-Wall", "-Wextra", "-Wno-missing-field-initializers", "-Wno-cast-function-type", "-ffunction-sections", "-fdata-sections"]
    headers = b"".join(p.read_bytes() for p in sorted((ROOT / "src").glob("*.hpp"))) + (ROOT / "src/pdf_api.inc").read_bytes()
    sources = ["main", "app", "backend", "renderer", "toolbar", "core"]
    def compile_one(name):
        source = ROOT / "src" / (name + ".cpp")
        obj = build / (name + ".o")
        digest = hashlib.sha256(source.read_bytes() + headers + repr(flags).encode()).hexdigest()
        stamp = obj.with_suffix(".hash")
        if obj.exists() and stamp.exists() and stamp.read_text() == digest:
            return obj
        run([compiler, *flags, "-c", source, "-o", obj])
        stamp.write_text(digest)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs, 8))) as executor:
        objects = list(executor.map(compile_one, sources))
    run([compiler, *objects, build / "app-res.o", "-o", package / "PaperView.exe", "-municode", "-mwindows", "-static", "-static-libgcc", "-static-libstdc++", "-s", "-Wl,--gc-sections", "-Wl,--nxcompat", "-Wl,--dynamicbase", "-Wl,--high-entropy-va", "-ld2d1", "-ldwrite", "-lcomctl32", "-lcomdlg32", "-lshell32", "-lole32", "-luuid", "-luxtheme", "-lbcrypt", "-lgdi32", "-luser32"])
    dll = ROOT / "vendor/pdfium/pypdfium2_raw/pdfium.dll"
    if not dll.exists():
        run([sys.executable, ROOT / "scripts/fetch_pdfium.py"])
    shutil.copy2(dll, package / "pdfium.dll")
    license_source = ROOT / "vendor/pdfium/pypdfium2-5.14.0.dist-info/licenses"
    shutil.copytree(license_source, package / "ThirdPartyLicenses", dirs_exist_ok=True)
    for source, target in [("docs/USER_GUIDE.zh-CN.md", "使用说明.txt"), ("docs/THIRD_PARTY.md", "THIRD_PARTY.txt")]:
        if (ROOT / source).exists():
            shutil.copy2(ROOT / source, package / target)
    artifacts = ROOT / "artifacts"
    artifacts.mkdir(exist_ok=True)
    if not args.skip_installer:
        run([find("makensis"), "-V2", "-DPAYLOAD=" + str(package), "-DOUTPUT=" + str(artifacts / "PDF-view-Setup-0.3.0-x64.exe"), ROOT / "packaging/installer.nsi"])
        shutil.make_archive(str(artifacts / "PDF-view-0.3.0-x64-portable"), "zip", package)
        files = sorted(p for p in artifacts.iterdir() if p.suffix in {".exe", ".zip"})
        (artifacts / "SHA256SUMS.txt").write_text("".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in files))
    print("Windows build:", package)

if __name__ == "__main__":
    main()
