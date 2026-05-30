#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    EFI_STATUS status;

    // --- 1. TAKE OVER GRAPHICS ---
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    status = uefi_call_wrapper(SystemTable->BootServices->LocateProtocol, 3, &gopGuid, NULL, (void**)&gop);
    if (EFI_ERROR(status)) while(1);

    // Clear the screen to pure black
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL black = {0, 0, 0, 0};
    uefi_call_wrapper(gop->Blt, 10, gop, &black, EfiBltVideoFill, 0, 0, 0, 0, 
                      gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution, 0);

    // --- 2. BULLETPROOF FILE SYSTEM SCANNER ---
    EFI_GUID fsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    UINTN handleCount = 0;
    EFI_HANDLE *handleBuffer = NULL;
    status = uefi_call_wrapper(SystemTable->BootServices->LocateHandleBuffer, 5, ByProtocol, &fsGuid, NULL, &handleCount, &handleBuffer);
    if (EFI_ERROR(status) || handleCount == 0) while(1); 

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *efiDir, *bootDir, *videoFile;
    BOOLEAN fileFound = FALSE;

    // Compiler-Proof Strings
    CHAR16 nameEFI[]  = {'E', 'F', 'I', 0};
    CHAR16 nameBOOT[] = {'B', 'O', 'O', 'T', 0};
    CHAR16 nameVIDEO[] = {'V', 'I', 'D', 'E', 'O', '.', 'B', 'P', 'L', 0};

    for (UINTN i = 0; i < handleCount; i++) {
        status = uefi_call_wrapper(SystemTable->BootServices->HandleProtocol, 3, handleBuffer[i], &fsGuid, (void**)&fs);
        if (EFI_ERROR(status)) continue;

        status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
        if (EFI_ERROR(status)) continue;

        // Try EFI
        status = uefi_call_wrapper(root->Open, 5, root, &efiDir, nameEFI, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            CHAR16 nameEfiLower[] = {'e', 'f', 'i', 0};
            status = uefi_call_wrapper(root->Open, 5, root, &efiDir, nameEfiLower, EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status)) continue;
        }

        // Try BOOT
        status = uefi_call_wrapper(efiDir->Open, 5, efiDir, &bootDir, nameBOOT, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            CHAR16 nameBootLower[] = {'b', 'o', 'o', 't', 0};
            status = uefi_call_wrapper(efiDir->Open, 5, efiDir, &bootDir, nameBootLower, EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status)) continue;
        }

        // Try VIDEO.BPL
        status = uefi_call_wrapper(bootDir->Open, 5, bootDir, &videoFile, nameVIDEO, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status)) {
            CHAR16 nameVideoLower[] = {'v', 'i', 'd', 'e', 'o', '.', 'b', 'p', 'l', 0};
            status = uefi_call_wrapper(bootDir->Open, 5, bootDir, &videoFile, nameVideoLower, EFI_FILE_MODE_READ, 0);
            if (EFI_ERROR(status)) continue;
        }

        fileFound = TRUE;
        break; // File is securely open!
    }

    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, handleBuffer);
    if (!fileFound) {
        // Flash RED if the file was deleted or lost
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL red = {0, 0, 255, 0};
        uefi_call_wrapper(gop->Blt, 10, gop, &red, EfiBltVideoFill, 0, 0, 0, 0, gop->Mode->Info->HorizontalResolution, gop->Mode->Info->VerticalResolution, 0);
        while(1);
    }

// --- 3. ALLOCATE THE RLE FRAME BUFFER ---
    UINTN frameSize = 1920 * 1080 * 4; 
    UINT32 *frameBuffer = NULL; // Using UINT32 array for easier pixel tracking
    
    status = uefi_call_wrapper(SystemTable->BootServices->AllocatePool, 3, EfiLoaderData, frameSize, (void**)&frameBuffer);
    if (EFI_ERROR(status)) while(1);

    // Calculate center of screen
    INT64 calcX = (gop->Mode->Info->HorizontalResolution - 1920) / 2;
    INT64 calcY = (gop->Mode->Info->VerticalResolution - 1080) / 2;

    // Clamp to 0 to prevent Unsigned Underflow if the screen is too small
    UINTN startX = (calcX < 0) ? 0 : (UINTN)calcX;
    UINTN startY = (calcY < 0) ? 0 : (UINTN)calcY;

    // --- 4. THE CUSTOM RLE CODEC LOOP ---
    UINT32 pixels_drawn = 0;
    UINT32 max_pixels = 1920 * 1080;

    while (1) {
        UINT32 chunk[2]; // chunk[0] = Count, chunk[1] = Color
        UINTN readSize = 8;
        
        // Read exactly one 8-byte instruction
        status = uefi_call_wrapper(videoFile->Read, 3, videoFile, &readSize, chunk);
        
        // Break if the video is over
        if (EFI_ERROR(status) || readSize < 8) break; 

        UINT32 count = chunk[0];
        UINT32 color = chunk[1];

        // Execute the instruction: Draw 'count' number of pixels
        for (UINT32 i = 0; i < count; i++) {
            frameBuffer[pixels_drawn] = color;
            pixels_drawn++;

            // If we have successfully filled an entire 1920x1080 frame...
            if (pixels_drawn == max_pixels) {
                
                // Blast the completed frame to the screen
                uefi_call_wrapper(gop->Blt, 10, gop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL*)frameBuffer, EfiBltBufferToVideo, 
                                  0, 0, startX, startY, 1920, 1080, 0);

                // Pause for 60 FPS
                uefi_call_wrapper(SystemTable->BootServices->Stall, 1, 16666);
                
                // Reset the tracker to 0 and begin drawing the next frame
                pixels_drawn = 0;
            }
        }
    }

    // --- 5. CLEANUP ---
    uefi_call_wrapper(SystemTable->BootServices->FreePool, 1, frameBuffer);

    // Reset the system to return to the firmware interface
    uefi_call_wrapper(SystemTable->RuntimeServices->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);    

    return EFI_SUCCESS;
}