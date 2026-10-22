  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF Binaries/negroni/QcomPkg/Drivers/EnvDxe/EnvDxe.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF Binaries/negroni/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/negroni/QcomPkg/Drivers/TzDxe/ScmDxeLA.inf
  INF Binaries/negroni/QcomPkg/Drivers/TzDxe/TzDxeLA.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF Binaries/negroni/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  INF Binaries/negroni/MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF Binaries/negroni/MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf

  INF negroniPkg/Drivers/NegroniBootMemoryDxe/NegroniBootMemoryDxe.inf
  INF negroniPkg/Drivers/NegroniKeyInputDxe/NegroniKeyInputDxe.inf
  INF negroniPkg/Drivers/NegroniTouchDxe/NegroniTouchDxe.inf

  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF Binaries/negroni/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/negroni/QcomPkg/Drivers/CPRDxe/CPRDxe.inf
!endif
  INF Binaries/negroni/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/SPIDxe/SPIDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/SdccDxe/SdccDxe.inf

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF Binaries/negroni/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

  INF Binaries/negroni/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/XhciDxe/XhciDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMouseDxe/UsbMouseDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF Binaries/negroni/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf

  INF Binaries/negroni/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/UsbInitDxe/UsbInitDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/UCDxe/UCDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/RNGDxe/RngDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/PILDxe/PILDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
  INF Binaries/negroni/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
