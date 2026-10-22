#!/bin/bash
# Apply the negroni changes to the submodules after `git submodule update --init --recursive`.
set -euo pipefail
cd "$(dirname "$0")/.."
git -C Mu_Basecore apply "$PWD/patches/mu_basecore-negroni.patch"
git -C Silicium-ACPI apply "$PWD/patches/silicium-acpi-negroni.patch"
echo "negroni patches applied"
