#!/usr/bin/env python3
"""Fetch the pinned Windows PDFium binary with PyPI's SHA-256 verification."""
import hashlib
import io
import pathlib
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
URL = "https://files.pythonhosted.org/packages/00/dc/ca7874924c9cfd701ad53f89529968523790e70473e0b71e834668316148/pypdfium2-5.14.0-py3-none-win_amd64.whl"
SHA256 = "149fd5c6397b8df8bf7911a93506eff0be874f877afe7ac936cf5d37d21a6a06"
DESTINATION = ROOT / "vendor" / "pdfium"

def main():
    with urllib.request.urlopen(URL, timeout=120) as response:
        payload = response.read()
    if hashlib.sha256(payload).hexdigest() != SHA256:
        raise RuntimeError("PDFium archive checksum mismatch")
    with zipfile.ZipFile(io.BytesIO(payload)) as archive:
        for name in archive.namelist():
            if name.endswith(".dll") or "license" in name.lower() or name.endswith("version.json"):
                destination = (DESTINATION / name).resolve()
                if not destination.is_relative_to(DESTINATION.resolve()):
                    raise RuntimeError("Unsafe archive path")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(name))
    print(f"Verified PDFium 156.0.8076.0 from pypdfium2 5.14.0: {SHA256}")

if __name__ == "__main__":
    main()
