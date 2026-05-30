all: main.efi

main.o: main.c
	gcc -O2 -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -I/usr/include/efi -I/usr/include/efi/x86_64 -I/usr/include/efi/protocol -DEFI_FUNCTION_WRAPPER -c main.c -o main.o

main.so: main.o
	ld -nostdlib -znocombreloc -T /usr/lib/elf_x86_64_efi.lds -shared -Bsymbolic -L /usr/lib /usr/lib/crt0-efi-x86_64.o main.o -o main.so -lefi -lgnuefi

main.efi: main.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -O efi-app-x86_64 main.so main.efi

clean:
	rm -f main.o main.so main.efi uefi.img OVMF_VARS.4m.fd