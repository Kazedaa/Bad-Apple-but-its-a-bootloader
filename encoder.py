import cv2
import numpy as np
import struct

# --- Configuration ---
INPUT_VIDEO = "bad_apple.mp4" # path to the input video file
OUTPUT_FILE = "video.bpl" # keep this the same or change the path in run.sh accordingly
WIDTH = 1920  # 1270 for QEMU simulation
HEIGHT = 1080 # 720 for QEMU simulation

print(f"Compiling {INPUT_VIDEO} into custom RLE format...")

cap = cv2.VideoCapture(INPUT_VIDEO)
frame_count = 0

active_color = -1
active_count = 0

with open(OUTPUT_FILE, "wb") as f:
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        
        # 1. Resize back to full 640x480
        frame = cv2.resize(frame, (WIDTH, HEIGHT))
        
        # 2. Convert to grayscale and FORCE pure Black or pure White
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        _, thresh = cv2.threshold(gray, 128, 255, cv2.THRESH_BINARY)
        
        # 3. Map to 32-bit UEFI Integers (Black = 0, White = 0x00FFFFFF)
        pixels = np.where(thresh.flatten() == 0, 0x00000000, 0x00FFFFFF).astype(np.uint32)
        
        # 4. Run-Length Encoding Logic
        diffs = np.diff(pixels)
        split_indices = np.where(diffs != 0)[0] + 1
        
        values = pixels[np.insert(split_indices, 0, 0)]
        counts = np.diff(np.append(np.insert(split_indices, 0, 0), pixels.size))
        
        for count, color in zip(counts, values):
            if color == active_color:
                active_count += count
            else:
                if active_count > 0:
                    # Write 8-byte instruction: [32-bit Count] [32-bit Color]
                    f.write(struct.pack('<II', int(active_count), int(active_color)))
                active_color = color
                active_count = count
                
        frame_count += 1
        if frame_count % 100 == 0:
            print(f"Processed {frame_count} frames...")

    # Write the final remaining chunk
    if active_count > 0:
        f.write(struct.pack('<II', int(active_count), int(active_color)))

cap.release()
print("Done! Check your file size. It should be tiny!")