# Mu-Silicium for the OnePlus 10 Pro 5G (negroni, SM8450)

Mu-Silicium with the negroni platform. Built DEBUG, wrapped by the ios-sweet shim and
booted by the OnePlus ABL from `boot_a`; it starts UniXNU/DarwinBoot (iOS 16) or Windows.

## Contents

- `Platforms/OnePlus/negroniPkg`: platform package (memory map, configuration map, key input,
  touch for the front page, boot memory for the UniXNU payload)
- `Resources/Configs/negroni.toml`, `Resources/DTBs/negroni.dtb`
- changes in `BootShim` and `Silicon/Silicium/SiliciumPkg` (BDS, SEC)
- `Common/Features/mu_feature_dfci`, `Common/Features/mu_feature_config` (front page)
- `patches/`: changes to the `Mu_Basecore` and `Silicium-ACPI` submodules

## Build

```sh
git submodule update --init --recursive
patches/apply.sh
python3 build_uefi.py -d negroni -r DEBUG
```

Output: `Build/negroniPkg/DEBUG_CLANGPDB/FV/SILICIUM_UEFI.fd`.

| Patch | Submodule | Base commit |
|---|---|---|
| `patches/mu_basecore-negroni.patch` | `Mu_Basecore` | `cfb93724a06ff86ff5ba971413368a7809ee42a5` |
| `patches/silicium-acpi-negroni.patch` | `Silicium-ACPI` | `2d465ad77c25de908f0ec7bdfcb57f865b61233c` |
