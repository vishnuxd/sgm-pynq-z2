# ============================================================================
# image_to_txt.py — Convert stereo image pair to TXT for Vitis HLS testbench
# ============================================================================
import cv2
import numpy as np

# --- CONFIGURATION ---
LEFT_FILE  = "sgm_test_left.png"
RIGHT_FILE = "sgm_test_right.png"
WIDTH  = 160
HEIGHT = 120

# --- LOAD BOTH IMAGES ---
left  = cv2.imread(LEFT_FILE,  cv2.IMREAD_GRAYSCALE)
right = cv2.imread(RIGHT_FILE, cv2.IMREAD_GRAYSCALE)

if left is None:
    print(f"ERROR: Could not load {LEFT_FILE}")
    exit()
if right is None:
    print(f"ERROR: Could not load {RIGHT_FILE}")
    exit()

print(f"Left:  {left.shape[1]} x {left.shape[0]}")
print(f"Right: {right.shape[1]} x {right.shape[0]}")

# --- CHECK SIZES MATCH ---
if left.shape != right.shape:
    print("WARNING: Left and right have different sizes!")
    print("Resizing right to match left...")
    right = cv2.resize(right, (left.shape[1], left.shape[0]))

# --- RESIZE TO 160x120 ---
left_small  = cv2.resize(left,  (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)
right_small = cv2.resize(right, (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)

# --- SAVE AS TXT ---
np.savetxt("left.txt",  left_small,  fmt='%d')
np.savetxt("right.txt", right_small, fmt='%d')

print(f"\nSaved left.txt and right.txt at {WIDTH}x{HEIGHT}")

# --- SAVE PREVIEWS WITH TITLES ---
left_preview  = cv2.resize(left_small,  (WIDTH*2, HEIGHT*2), interpolation=cv2.INTER_NEAREST)
right_preview = cv2.resize(right_small, (WIDTH*2, HEIGHT*2), interpolation=cv2.INTER_NEAREST)

cv2.putText(left_preview,  "Left Input",  (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)
cv2.putText(right_preview, "Right Input", (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX, 0.9, (255, 255, 255), 2)

cv2.imwrite("preview_left.png",  left_preview)
cv2.imwrite("preview_right.png", right_preview)
print("Saved preview_left.png and preview_right.png")