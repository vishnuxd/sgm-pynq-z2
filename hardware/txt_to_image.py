# ============================================================================
# txt_to_image.py — Visualize disparity.txt from Vitis HLS C-Sim
# ============================================================================
import numpy as np
import cv2

WIDTH  = 160
HEIGHT = 120

# --- Load disparity values ---
disp = np.zeros((HEIGHT, WIDTH), dtype=np.uint8)
with open("disparity.txt", "r") as f:
    for y in range(HEIGHT):
        line = f.readline().strip().split()
        for x in range(WIDTH):
            disp[y][x] = int(line[x])

print(f"Loaded disparity: {WIDTH}x{HEIGHT}")
print(f"Min: {disp.min()}, Max: {disp.max()}, Mean: {disp.mean():.2f}")

# --- Normalize and colorize ---
disp_norm = (disp.astype(np.float32) / max(disp.max(), 1) * 255).astype(np.uint8)
disp_color = cv2.applyColorMap(disp_norm, cv2.COLORMAP_TURBO)
disp_large = cv2.resize(disp_color, (640, 480), interpolation=cv2.INTER_CUBIC)

# --- Add title ---
cv2.putText(disp_large, "SGM Depth Map", (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)

# --- Save ---
cv2.imwrite("disparity_large.png", disp_large)
print("Saved disparity_large.png")