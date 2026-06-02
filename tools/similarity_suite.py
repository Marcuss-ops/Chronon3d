#!/usr/bin/env python3
import argparse
import sys
import os
import cv2
import numpy as np
from skimage.metrics import structural_similarity as ssim_func

try:
    import pytesseract
except ImportError:
    pytesseract = None

def calculate_ssim(img1, img2):
    # Convert to grayscale
    gray1 = cv2.cvtColor(img1, cv2.COLOR_BGR2GRAY)
    gray2 = cv2.cvtColor(img2, cv2.COLOR_BGR2GRAY)
    score, _ = ssim_func(gray1, gray2, full=True)
    return score

def calculate_psnr_mse(img1, img2):
    mse = np.mean((img1.astype(np.float64) - img2.astype(np.float64)) ** 2)
    if mse < 1e-6:
        return 0.0, 100.0
    max_pixel = 255.0
    psnr = 20 * np.log10(max_pixel / np.sqrt(mse))
    return mse, psnr

def calculate_palette_score(img1, img2):
    # Convert to HSV
    hsv1 = cv2.cvtColor(img1, cv2.COLOR_BGR2HSV)
    hsv2 = cv2.cvtColor(img2, cv2.COLOR_BGR2HSV)
    
    # Calculate histograms for H and S channels
    hist1 = cv2.calcHist([hsv1], [0, 1], None, [180, 256], [0, 180, 0, 256])
    hist2 = cv2.calcHist([hsv2], [0, 1], None, [180, 256], [0, 180, 0, 256])
    
    # Normalize
    cv2.normalize(hist1, hist1, alpha=0, beta=1, norm_type=cv2.NORM_MINMAX)
    cv2.normalize(hist2, hist2, alpha=0, beta=1, norm_type=cv2.NORM_MINMAX)
    
    # Compare using correlation
    score = cv2.compareHist(hist1, hist2, cv2.HISTCMP_CORREL)
    return max(0.0, score)

def calculate_edge_similarity(img1, img2):
    # Convert to grayscale & blur to reduce noise
    gray1 = cv2.GaussianBlur(cv2.cvtColor(img1, cv2.COLOR_BGR2GRAY), (5, 5), 0)
    gray2 = cv2.GaussianBlur(cv2.cvtColor(img2, cv2.COLOR_BGR2GRAY), (5, 5), 0)
    
    # Canny edges
    edges1 = cv2.Canny(gray1, 50, 150)
    edges2 = cv2.Canny(gray2, 50, 150)
    
    # Match edges by counting overlapping pixels (with small dilation to allow tolerance)
    kernel = np.ones((3, 3), np.uint8)
    dilated2 = cv2.dilate(edges2, kernel, iterations=1)
    
    overlap = np.logical_and(edges1 > 0, dilated2 > 0)
    total_edges = np.sum(edges1 > 0)
    
    if total_edges == 0:
        return 1.0
    return float(np.sum(overlap)) / float(total_edges)

def calculate_ocr_layout_score(img1, img2):
    if not pytesseract:
        return 0.5 # Default fallback
    
    try:
        # Get OCR data for both images
        data1 = pytesseract.image_to_data(img1, output_type=pytesseract.Output.DICT)
        data2 = pytesseract.image_to_data(img2, output_type=pytesseract.Output.DICT)
        
        words1 = [w.lower().strip() for w in data1['text'] if w.strip()]
        words2 = [w.lower().strip() for w in data2['text'] if w.strip()]
        
        if not words1 and not words2:
            return 1.0
        if not words1 or not words2:
            return 0.0
            
        # Match common words and look at bounding boxes
        common_words = set(words1).intersection(set(words2))
        if not common_words:
            return 0.1
            
        # Calculate IoU score on matched word bounding boxes
        scores = []
        for word in common_words:
            # Find coordinates of first occurrence in both
            i1 = words1.index(word)
            i2 = words2.index(word)
            
            box1 = (data1['left'][i1], data1['top'][i1], data1['width'][i1], data1['height'][i1])
            box2 = (data2['left'][i2], data2['top'][i2], data2['width'][i2], data2['height'][i2])
            
            # IoU
            x1 = max(box1[0], box2[0])
            y1 = max(box1[1], box2[1])
            x2 = min(box1[0] + box1[2], box2[0] + box2[2])
            y2 = min(box1[1] + box1[3], box2[1] + box2[3])
            
            inter_area = max(0, x2 - x1) * max(0, y2 - y1)
            box1_area = box1[2] * box1[3]
            box2_area = box2[2] * box2[3]
            union_area = box1_area + box2_area - inter_area
            
            iou = inter_area / float(union_area) if union_area > 0 else 0.0
            scores.append(iou)
            
        return float(np.mean(scores))
    except Exception:
        return 0.5

def main():
    parser = argparse.ArgumentParser(description="Chronon3d Visual Similarity Suite")
    parser.add_argument("--reference", required=True, help="Path to reference target image")
    parser.add_argument("--render", required=True, help="Path to rendered output image")
    parser.add_argument("--diff-output", default=None, help="Optional path to save visual diff file")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.reference):
        print(f"Error: Reference file does not exist: {args.reference}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(args.render):
        print(f"Error: Rendered file does not exist: {args.render}", file=sys.stderr)
        sys.exit(1)
        
    img1 = cv2.imread(args.reference)
    img2 = cv2.imread(args.render)
    
    # Ensure they have the same size, resize if needed to match reference dimensions
    if img1.shape != img2.shape:
        img2 = cv2.resize(img2, (img1.shape[1], img1.shape[0]))
        
    # Calculate similarity metrics
    ssim = calculate_ssim(img1, img2)
    mse, psnr = calculate_psnr_mse(img1, img2)
    palette_score = calculate_palette_score(img1, img2)
    edge_score = calculate_edge_similarity(img1, img2)
    ocr_layout_score = calculate_ocr_layout_score(img1, img2)
    
    # Compute Final Weighted Match Score
    # 35% SSIM, 25% Edge, 20% Palette, 20% OCR Layout
    final_score = (
        0.35 * ssim +
        0.25 * edge_score +
        0.20 * palette_score +
        0.20 * ocr_layout_score
    ) * 100.0
    
    print("=======================================")
    print("      CHRONON3D SIMILARITY METRICS     ")
    print("=======================================")
    print(f"  SSIM (Structural):       {ssim:.4f}")
    print(f"  MSE:                     {mse:.2f}")
    print(f"  PSNR:                    {psnr:.2f} dB")
    print(f"  Palette Correlation:     {palette_score:.4f}")
    print(f"  Edge Alignment:          {edge_score:.4f}")
    print(f"  OCR Layout Similarity:   {ocr_layout_score:.4f}")
    print("---------------------------------------")
    print(f"  VISUAL MATCH SCORE:      {final_score:.2f} / 100.0")
    print("=======================================")
    
    # Save visual diff image if requested
    if args.diff_output:
        diff = cv2.absdiff(img1, img2)
        cv2.imwrite(args.diff_output, diff)
        print(f"Visual diff saved to: {args.diff_output}")

if __name__ == "__main__":
    main()
