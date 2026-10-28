"""pip install .   -> builds the C library and installs the `tdbg` Python package."""
from setuptools import Extension, setup

lib = Extension(
    "tdbg._libtdbg",
    sources=["src/common.c", "src/qr.c", "src/datamatrix.c", "src/aztec.c",
             "src/maxicode.c", "python/tdbg/_stub.c"],
    include_dirs=["include", "src"],
    define_macros=[("TDBG_BUILD", "1")],
)

setup(
    name="tdbg",
    version="1.0.0",
    description="2D Barcodes Generator: QR Code, Data Matrix, Aztec and MaxiCode (C core, ctypes binding)",
    packages=["tdbg"],
    package_dir={"": "python"},
    ext_modules=[lib],
    python_requires=">=3.7",
)
