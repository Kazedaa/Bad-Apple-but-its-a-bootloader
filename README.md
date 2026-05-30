# BadAppleOS
BadAppleOS is a standalone UEFI application written in C that functions as a bare-metal micro-operating system. Its primary purpose is to take control of a computer's graphics hardware during the boot sequence to stream video directly to the screen.



## Architecture
The application interfaces directly with the motherboard's firmware. It utilizes the UEFI Graphics Output Protocol (GOP) to access the display's framebuffer at native resolutions and full color depth.

To circumvent the 4-Gigabyte maximum file size limitation inherent to FAT32 filesystems , the video data is heavily compressed into a custom 1-bit Run-Length Encoded (RLE) binary format. The application reads this compressed asset directly from the storage media using the `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`, employing explicit directory traversal (opening folders sequentially) to avoid path-parsing quirks common in modern UEFI firmware.

 - `main.c`: The core C engine. It initializes the UEFI environment, manually navigates the EFI and BOOT directories to locate the video file, and allocates a memory buffer for rendering. The engine decodes the RLE instructions on the fly and utilizes the GOP Blt function (with dynamic cropping via the Delta parameter) to push pixels to the screen. Playback speed is controlled using the UEFI Stall function set to 16,666 microseconds to maintain a 60 FPS framerate. Upon completion, it calls ResetSystem to reboot the hardware.

 - `encoder.py`: A Python script that uses OpenCV to process standard video files. It thresholds every pixel into strict black or white values, counts the consecutive runs of pixels, and writes them out as custom 8-byte RLE instructions and saves it as a .bpl (short for bad apple) file.

 - `Makefile`: Contains the build instructions to compile the C code into a shared object and then uses `objcopy` to format it into a native `.efi` executable.

 - `run.sh`: A shell script used for simulation. It automates the compilation process, uses parted and mtools to dynamically generate a 500MB GUID Partition Table (GPT) virtual hard drive formatted to FAT32, injects the executable and video files, and launches QEMU.

## Dependencies
To build and test this project, your environment requires the following packages:
 - gcc and make (for compilation) 
 - gnu-efi (UEFI headers and bindings) 
 - qemu-full and edk2-ovmf (for virtual machine simulation) 
 - mtools and parted (for virtual FAT32 drive creation and injection) 
 - python-opencv (for video encoding)

## Usage
### Encode the video
```bash
python convert.py 
```
Configure the input file 
### Emulation (QEMU)
```bash
./run.sh
```
QEMU simulation is capped at 1270x720. Change the resolution in `encoder.py`
QEMU simulation may display the video at a different speed due to emulator overhead. Adjust the Stall timing in main.c if needed for testing. 

### Bare Metal Deployment
To run the application on physical hardware:
 - Format a USB drive with a GUID Partition Table (GPT) and allocate a single FAT32 partition.

THIS WILL WIPE OUT YOUR DRIVE, make sure you have backed up the contents.

```bash
sudo dd if=/dev/zero of=/dev/sdX bs=1M count=10 status=progress
sudo parted /dev/sdX mklabel gpt
sudo parted /dev/sdX mkpart ESP fat32 1MiB 500MiB
sudo parted /dev/sdX set 1 esp on
sudo mkfs.fat -F 32 /dev/sdX1
```
 - Create the standard UEFI boot folder structure on the USB drive: `/EFI/BOOT/`.
```bash
sudo mount /dev/sdX1 /mnt
mkdir -p /EFI/BOOT
```
 - Compile the C code (make) and copy the resulting executable to the USB drive, renaming it strictly to `BOOTX64.EFI`.
```bash
make 
sudo cp main.efi /mnt/EFI/BOOT/BOOTX64.EFI
```
 - Copy your generated video binary into the same /EFI/BOOT/ folder on the USB drive.
```bash
python encoder.py
sudo cp video.bpl /mnt/EFI/BOOT/VIDEO.BPL
```
 - Reboot your machine and select the USB drive from the BIOS boot menu.