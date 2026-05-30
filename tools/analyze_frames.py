import cv2
import numpy as np
import glob
import os

frames = sorted(glob.glob("build/extracted_output/frame_*.png"))
print(f"Found {len(frames)} frames to analyze.")

for f in frames:
    img = cv2.imread(f, cv2.IMREAD_GRAYSCALE)
    if img is None:
        continue
    # Crop to center text area (1920x1080 canvas)
    img = img[440:640, 600:1320]
    # Threshold to get text mask (grid is faint, text is bright)
    _, thresh = cv2.threshold(img, 200, 255, cv2.THRESH_BINARY)
    # Find contours
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        print(f"{os.path.basename(f)}: No text found")
        continue
    
    # Get the bounding rect of all contours combined
    x_min, y_min = img.shape[1], img.shape[0]
    x_max, y_max = 0, 0
    for c in contours:
        x, y, w, h = cv2.boundingRect(c)
        x_min = min(x_min, x)
        y_min = min(y_min, y)
        x_max = max(x_max, x + w)
        y_max = max(y_max, y + h)
        
    width = x_max - x_min
    height = y_max - y_min
    center_x = x_min + width / 2.0
    center_y = y_min + height / 2.0
    
    # Calculate moments to find angle of orientation
    M = cv2.moments(thresh)
    if M["m00"] != 0:
        cx = int(M["m10"] / M["m00"])
        cy = int(M["m01"] / M["m00"])
        # Angle of rotation
        mu20 = M["mu20"] / M["m00"]
        mu02 = M["mu02"] / M["m00"]
        mu11 = M["mu11"] / M["m00"]
        # orientation angle
        angle = 0.5 * np.arctan2(2 * mu11, mu20 - mu02) * (180.0 / np.pi)
    else:
        cx, cy, angle = 0, 0, 0
        
    print(f"{os.path.basename(f)}: center=({center_x:.1f}, {center_y:.1f}) size=({width}, {height}) angle={angle:.2f}")
