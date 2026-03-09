from __future__ import annotations

import os
from pathlib import Path

from setuptools import Distribution, find_namespace_packages, setup


def package_files(root: Path, package_root: Path) -> list[str]:
    files: list[str] = []
    if not root.is_dir():
        return files
    for path in root.rglob("*"):
        if path.is_file():
            files.append(str(path.relative_to(package_root)))
    return files


class BinaryDistribution(Distribution):
    def has_ext_modules(self) -> bool:
        return True


ROOT = Path(__file__).resolve().parent
PACKAGE_ROOT = ROOT / "pycircuit"

setup(
    name="pycircuit",
    version=os.environ["PYC_WHEEL_VERSION"],
    description="A Python-based hardware description framework that compiles Python to RTL through MLIR",
    long_description=(ROOT / "README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    license="MIT",
    python_requires=">=3.9",
    install_requires=[
        "click>=8.0.0",
        "pyyaml>=6.0",
    ],
    packages=find_namespace_packages(include=["pycircuit", "pycircuit.*"], exclude=["pycircuit._toolchain*"]),
    package_data={
        "pycircuit": package_files(PACKAGE_ROOT / "_toolchain", PACKAGE_ROOT)
        + package_files(PACKAGE_ROOT / "_tools", PACKAGE_ROOT),
    },
    include_package_data=True,
    zip_safe=False,
    entry_points={
        "console_scripts": [
            "pycircuit=pycircuit.cli:main",
            "pycc=pycircuit.packaged_toolchain:main",
            "pyc-opt=pycircuit.packaged_toolchain:pyc_opt_main",
        ]
    },
    distclass=BinaryDistribution,
)
