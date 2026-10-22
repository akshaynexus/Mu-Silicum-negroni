#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.

#  SPDX-License-Identifier: BSD-2-Clause-Patent

[Defines]
  PLATFORM_NAME                  = negroni
  PLATFORM_GUID                  = 1F8B6A9C-41F2-4C2C-8A91-B8C03F8ED7E6
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/negroniPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = negroniPkg/negroni.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include WaipioPkg/WaipioPkg.dsc.inc

[PcdsFixedAtBuild]

  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x0F

  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"OnePlus 10 Pro 5G"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"negroni"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"OnePlus_10_Pro_5G_negroni"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"OnePlus 10 Pro 5G"

  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2412
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  MemoryMapLib|negroniPkg/Library/MemoryMapLib/MemoryMapLib.inf

  ConfigurationMapLib|negroniPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
  negroniPkg/Drivers/NegroniBootMemoryDxe/NegroniBootMemoryDxe.inf
  negroniPkg/Applications/NegroniXnuBoot/NegroniXnuBoot.inf
  negroniPkg/Drivers/NegroniKeyInputDxe/NegroniKeyInputDxe.inf
  negroniPkg/Drivers/NegroniTouchDxe/NegroniTouchDxe.inf
  negroniPkg/Drivers/NegroniWindowsFirstDxe/NegroniWindowsFirstDxe.inf
  negroniPkg/Drivers/NegroniPcieDxe/NegroniPcieDxe.inf
  MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf

  negroni/AcpiTables.inf

!include negroniPkg/Include/Frontpage.dsc.inc
