#include <efi.h>
#include <efilib.h>

void FatalPanic(EFI_SYSTEM_TABLE *SystemTable, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT8 r, UINT8 g, UINT8 b) {
    if (gop) {
        // Flash the screen with the error color (B, G, R, Reserved)
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL color = {b, g, r, 0};
        uefi_call_wrapper(gop->Blt, 10, gop, &color, EfiBltVideoFill, 0, 0, 0, 0, 
                          gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution, 0);
    }
    // Wait 3 seconds 
    uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 3000000); 
    // Hard reboot
    uefi_call_wrapper(SystemTable->RuntimeServices->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    EFI_STATUS status;

    // --- Graphics Output Protocol ---
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
    // Flash Red
    if (EFI_ERROR(status)) FatalPanic(SystemTable, gop, 255, 0, 0);

    // Clear the screen to pure black
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};
    uefi_call_wrapper(gop->Blt, 10, gop, &black, EfiBltVideoFill, 0, 0, 0, 0, 
                      gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution, 0);

    // --- FILE SYSTEM SCANNER ---
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    UINTN handleCount = 0;
    EFI_HANDLE *handleBuffer = NULL;
    status = uefi_call_wrapper(SystemTable->BootServices->LocateHandleBuffer, 5, ByProtocol, &fsGuid, NULL, &handleCount, &handleBuffer);
    // Flash Red
    if (EFI_ERROR(status) || handleCount == 0) FatalPanic(SystemTable, gop, 255, 0, 0); 

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *efiDir, *bootDir, *videoFile;
    BOOLEAN fileFound = FALSE;

    // Literals
    CHAR16 nameEFI[]  = {'E', 'F', 'I', 0};
    CHAR16 nameBOOT[] = {'B', 'O', 'O', 'T', 0};
    CHAR16 nameVIDEO[] = {'V', 'I', 'D', 'E', 'O', '.', 'B', 'P', 'L', 0};

    for (UINTN i = 0; i < handleCount; i++) {
        status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, handleBuffer[i], &fsGuid, (void**)&fs);
        if (EFI_ERROR(status)) continue;

        status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
        if (EFI_ERROR(status)) continue;

        // Try EFI -> BOOT -> VIDEO.BPL
        status = uefi_call_wrapper(root->Open, 5, root, &efiDir, nameEFI, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(root->Close, 1, root); 
            continue; 
        }

        status = uefi_call_wrapper(efiDir->Open, 5, efiDir, &bootDir, nameBOOT, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(efiDir->Close, 1, efiDir);
            uefi_call_wrapper(root->Close, 1, root);
            continue;
        }

        status = uefi_call_wrapper(bootDir->Open, 5, bootDir, &videoFile, nameVIDEO, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(bootDir->Close, 1, bootDir);
            uefi_call_wrapper(efiDir->Close, 1, efiDir);
            uefi_call_wrapper(root->Close, 1, root);
            continue;
        }

        fileFound = TRUE;
        uefi_call_wrapper(bootDir->Close, 1, bootDir);
        uefi_call_wrapper(efiDir->Close, 1, efiDir);
        uefi_call_wrapper(root->Close, 1, root);
        break; 
    }

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, handleBuffer);
    // Flash yellow if file not found
    if (!fileFound) FatalPanic(SystemTable, gop, 255, 255, 0);

    // --- ALLOCATE THE RLE FRAME BUFFER ---
    UINT32 resX = 1920; 
    UINT32 resY = 1080;
    UINTN frameSize = resX * resY * 4; 
    UINT32 *frameBuffer = NULL;
    
    status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, frameSize, (void**)&frameBuffer);
    // Flash Red
    if (EFI_ERROR(status)) FatalPanic(SystemTable, gop, 255, 0, 0);

    // Calculate center of screen
    INT64 calcX = (gop->Mode->Info->HorizontalResolution - resX) / 2;
    INT64 calcY = (gop->Mode->Info->VerticalResolution - resY) / 2;

    // Clamp to 0 to prevent Unsigned Underflow if the screen is too small
    UINTN startX = (calcX < 0) ? 0 : (UINTN)calcX;
    UINTN startY = (calcY < 0) ? 0 : (UINTN)calcY;

    // --- LOAD ENTIRE VIDEO INTO RAM ---
    // Allocate 500MB for the video file
    UINTN maxFileSize = 500 * 1024 * 1024; 
    UINT8 *videoBuffer = NULL;
    status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, maxFileSize, (void**)&videoBuffer);
    if (EFI_ERROR(status)) {
        // Flash BLUE if out of memory
        FatalPanic(SystemTable, gop, 0, 0, 255);
    }

    UINTN totalBytesRead = 0;
    UINTN chunkSize = 10 * 1024 * 1024; // Read in 10MB chunks for UEFI stability

    while (totalBytesRead < maxFileSize) {
        UINTN bytesToRead = chunkSize;
        // Don't read past our 500MB allocated buffer
        if (totalBytesRead + bytesToRead > maxFileSize) {
            bytesToRead = maxFileSize - totalBytesRead;
        }
        
        status = uefi_call_wrapper(videoFile->Read, 3, videoFile, &bytesToRead, videoBuffer + totalBytesRead);
        
        // Break if we hit End of File or an error
        if (EFI_ERROR(status) || bytesToRead == 0) break; 
        
        totalBytesRead += bytesToRead;
    }

    // We are done with the disk! Close the file.
    uefi_call_wrapper(videoFile->Close, 1, videoFile);


    // --- 5. THE CUSTOM RLE CODEC LOOP (RAM-BASED) ---
    UINT32 pixels_drawn = 0;
    UINT32 max_pixels = resX * resY;
    UINTN bufferOffset = 0;

    // Calculate bounds to prevent memory corruption on smaller screens
    UINTN drawWidth  = (gop->Mode->Info->HorizontalResolution < resX) ? gop->Mode->Info->HorizontalResolution : resX;
    UINTN drawHeight = (gop->Mode->Info->VerticalResolution < resY) ? gop->Mode->Info->VerticalResolution : resY;

    while (bufferOffset + 8 <= totalBytesRead) {
        // Read 8 bytes directly from RAM
        UINT32 count = *((UINT32*)(videoBuffer + bufferOffset));
        UINT32 color = *((UINT32*)(videoBuffer + bufferOffset + 4));
        bufferOffset += 8;

        // Execute the instruction
        for (UINT32 i = 0; i < count; i++) {
            frameBuffer[pixels_drawn] = color;
            pixels_drawn++;

            // If frame is complete
            if (pixels_drawn == max_pixels) {
                
                // Get the direct memory address of the GPU
                UINT32 *gpuBuffer = (UINT32*)(UINTN)gop->Mode->FrameBufferBase;
                
                // The GPU might have invisible padding at the edge of the screen.
                UINT32 pitch = gop->Mode->Info->PixelsPerScanLine;

                // Copy our completed frame safely, row by row
                for (UINTN y = 0; y < drawHeight; y++) {
                    UINT32 *destRow = &gpuBuffer[(startY + y) * pitch + startX];
                    UINT32 *srcRow  = &frameBuffer[y * resX]; // Always read from the resY buffer
                    
                    // Copy ONLY the safe width (width * 4 bytes per pixel)
                    uefi_call_wrapper(SystemTable->BootServices->CopyMem, 3, destRow, srcRow, drawWidth * 4);
                }

                // STALL: To adjust FPS
                uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 33333); // ~30 FPS
                
                pixels_drawn = 0;
            }
        }
    }

    // --- CLEANUP ---
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, videoBuffer);
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, frameBuffer);

    // Reset the system
    uefi_call_wrapper(SystemTable->RuntimeServices->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);    

    return EFI_SUCCESS;
}