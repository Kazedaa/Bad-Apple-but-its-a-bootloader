#!/bin/bash
set -e

echo "--- Cleaning Old Builds ---"
make clean

echo "--- Compiling Build ---"
make

if [ ! -f main.efi ]; then
    echo "ERROR: main.efi was not created!"
    exit 1
fi

echo "--- Building 500MB Virtual Hard Drive ---"
dd if=/dev/zero of=uefi.img bs=1M count=500 status=none
parted -s uefi.img mklabel gpt mkpart ESP fat32 1MiB 199MiB set 1 esp on

echo "--- Formatting Virtual USB ---"
mformat -i uefi.img@@1M -v BADAOS -F ::
mmd -i uefi.img@@1M ::/EFI
mmd -i uefi.img@@1M ::/EFI/BOOT
mcopy -i uefi.img@@1M main.efi ::/EFI/BOOT/BOOTX64.EFI

echo "--- Injecting Video---"
mcopy -i uefi.img@@1M video.bpl ::/EFI/BOOT/VIDEO.BPL

echo "--- THE BOOT FOLDER ---"
mdir -i uefi.img@@1M ::/EFI/BOOT

echo "--- Booting QEMU ---"
cp /usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd ./OVMF_VARS.4m.fd
qemu-system-x86_64 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd \
  -drive if=pflash,format=raw,file=OVMF_VARS.4m.fd \
  -drive format=raw,file=uefi.img \
  -net none -no-reboot