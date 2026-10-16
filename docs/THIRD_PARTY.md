# Third-party components

PaperView uses PDFium 156.0.8076.0 distributed in pypdfium2 5.14.0 Windows x64 wheel. The wheel is pinned and SHA-256 verified by scripts/fetch_pdfium.py. PDFium and bundled dependency notices are reproduced in ThirdPartyLicenses. The Python wrapper is not needed at runtime.

The application is compiled with MinGW-w64/GCC; GCC runtime components are covered by their runtime library exception. The installer is built with NSIS. Windows system libraries (Direct2D, DirectWrite and common controls) are supplied by Windows.
