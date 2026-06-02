#!/usr/bin/env python3
import argparse
import sys
import os
import cv2
import numpy as np
import hashlib
import json
import subprocess
from skimage.metrics import structural_similarity as ssim_func
from PIL import Image

try:
    import pytesseract
except ImportError:
    pytesseract = None

# =====================================================================
# 1. PIXEL TESTS
# =====================================================================
def run_pixel_tests(img1, img2):
    diff = img1.astype(np.float64) - img2.astype(np.float64)
    mse = np.mean(diff ** 2)
    rmse = np.sqrt(mse)
    mae = np.mean(np.abs(diff))
    
    # PSNR
    if mse < 1e-6:
        psnr = 100.0
    else:
        psnr = 20 * np.log10(255.0 / rmse)
        
    max_pixel_err = np.max(np.abs(diff))
    
    # Mean Absolute Alpha Error (if 4 channels)
    alpha_err = 0.0
    if img1.shape[2] == 4 and img2.shape[2] == 4:
        alpha_err = np.mean(np.abs(img1[:, :, 3].astype(np.float64) - img2[:, :, 3].astype(np.float64)))
        
    return {
        "mse": mse,
        "rmse": rmse,
        "mae": mae,
        "psnr": psnr,
        "max_pixel_error": max_pixel_err,
        "mean_absolute_alpha_error": alpha_err
    }

# =====================================================================
# 2. PERCEPTUAL TESTS (SSIM & MS-SSIM)
# =====================================================================
def run_perceptual_tests(img1, img2):
    # Grayscale conversion
    g1 = cv2.cvtColor(img1, cv2.COLOR_BGR2GRAY)
    g2 = cv2.cvtColor(img2, cv2.COLOR_BGR2GRAY)
    
    # Standard SSIM
    ssim, _ = ssim_func(g1, g2, full=True)
    
    # Multi-Scale SSIM approximation
    ms_ssim_weights = [0.0448, 0.2856, 0.3001, 0.2363, 0.1333]
    ms_ssim_scores = []
    curr1, curr2 = g1.copy(), g2.copy()
    
    for _ in range(len(ms_ssim_weights)):
        if curr1.shape[0] < 11 or curr1.shape[1] < 11:
            break
        s, _ = ssim_func(curr1, curr2, full=True)
        ms_ssim_scores.append(s)
        curr1 = cv2.resize(curr1, (curr1.shape[1] // 2, curr1.shape[0] // 2), interpolation=cv2.INTER_AREA)
        curr2 = cv2.resize(curr2, (curr2.shape[1] // 2, curr2.shape[0] // 2), interpolation=cv2.INTER_AREA)
        
    # Weighted MS-SSIM
    if ms_ssim_scores:
        w = ms_ssim_weights[:len(ms_ssim_scores)]
        w = [x / sum(w) for x in w]
        ms_ssim = sum(s * wt for s, wt in zip(ms_ssim_scores, w))
    else:
        ms_ssim = ssim
        
    return {
        "ssim": float(ssim),
        "ms_ssim": float(ms_ssim)
    }

# =====================================================================
# 3. LAYOUT & TEXT OCR TESTS
# =====================================================================
def run_layout_tests(img1, img2):
    if not pytesseract:
        return {"ocr_layout_iou": 0.0, "center_error_px": 999.0, "scale_error": 1.0}
        
    try:
        data1 = pytesseract.image_to_data(img1, output_type=pytesseract.Output.DICT)
        data2 = pytesseract.image_to_data(img2, output_type=pytesseract.Output.DICT)
        
        words1 = [w.lower().strip() for w in data1['text'] if w.strip()]
        words2 = [w.lower().strip() for w in data2['text'] if w.strip()]
        
        common = set(words1).intersection(set(words2))
        if not common:
            return {"ocr_layout_iou": 0.0, "center_error_px": 999.0, "scale_error": 1.0}
            
        ious = []
        center_errors = []
        scale_errors = []
        
        for w in common:
            i1 = words1.index(w)
            i2 = words2.index(w)
            
            b1 = (data1['left'][i1], data1['top'][i1], data1['width'][i1], data1['height'][i1])
            b2 = (data2['left'][i2], data2['top'][i2], data2['width'][i2], data2['height'][i2])
            
            # IoU
            x1 = max(b1[0], b2[0])
            y1 = max(b1[1], b2[1])
            x2 = min(b1[0] + b1[2], b2[0] + b2[2])
            y2 = min(b1[1] + b1[3], b2[1] + b2[3])
            
            inter = max(0, x2 - x1) * max(0, y2 - y1)
            union = (b1[2] * b1[3]) + (b2[2] * b2[3]) - inter
            ious.append(inter / float(union) if union > 0 else 0.0)
            
            # Center Distance
            c1 = (b1[0] + b1[2]/2.0, b1[1] + b1[3]/2.0)
            c2 = (b2[0] + b2[2]/2.0, b2[1] + b2[3]/2.0)
            dist = np.sqrt((c1[0] - c2[0])**2 + (c1[1] - c2[1])**2)
            center_errors.append(dist)
            
            # Scale Error
            a1 = b1[2] * b1[3]
            a2 = b2[2] * b2[3]
            se = abs(a1 - a2) / max(a1, a2)
            scale_errors.append(se)
            
        return {
            "ocr_layout_iou": float(np.mean(ious)),
            "center_error_px": float(np.mean(center_errors)),
            "scale_error": float(np.mean(scale_errors))
        }
    except Exception:
        return {"ocr_layout_iou": 0.0, "center_error_px": 999.0, "scale_error": 1.0}

# =====================================================================
# 4. COMPOSITION & GLOBAL TESTS
# =====================================================================
def run_composition_tests(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    
    # Center of mass (Visual center)
    moments = cv2.moments(gray)
    if moments["m00"] > 0:
        cx = moments["m10"] / moments["m00"]
        cy = moments["m01"] / moments["m00"]
    else:
        cx, cy = w / 2, h / 2
        
    optical_center_offset = np.sqrt((cx - w/2)**2 + (cy - h/2)**2)
    
    # Left-Right balance & Top-Bottom balance
    left = np.mean(gray[:, :w//2])
    right = np.mean(gray[:, w//2:])
    top = np.mean(gray[:h//2, :])
    bottom = np.mean(gray[h//2:, :])
    
    lr_balance = abs(left - right) / max(1.0, left + right)
    tb_balance = abs(top - bottom) / max(1.0, top + bottom)
    
    # Empty space ratio (thresholding below a dark background value)
    empty_pixels = np.sum(gray < 15)
    empty_ratio = empty_pixels / (w * h)
    
    return {
        "visual_center_offset_px": float(optical_center_offset),
        "left_right_unbalance": float(lr_balance),
        "top_bottom_unbalance": float(tb_balance),
        "empty_space_ratio": float(empty_ratio)
    }

# =====================================================================
# 5. COLOR & PALETTE TESTS
# =====================================================================
def run_color_tests(img1, img2):
    # Lab Color Space average delta E
    lab1 = cv2.cvtColor(img1, cv2.COLOR_BGR2Lab)
    lab2 = cv2.cvtColor(img2, cv2.COLOR_BGR2Lab)
    
    mean1 = np.mean(lab1, axis=(0, 1))
    mean2 = np.mean(lab2, axis=(0, 1))
    
    # Delta E CIE76 approximation
    delta_e = np.sqrt(np.sum((mean1 - mean2) ** 2))
    
    # HSV distributions
    hsv1 = cv2.cvtColor(img1, cv2.COLOR_BGR2HSV)
    hsv2 = cv2.cvtColor(img2, cv2.COLOR_BGR2HSV)
    
    sat1, val1 = hsv1[:, :, 1], hsv1[:, :, 2]
    sat2, val2 = hsv2[:, :, 1], hsv2[:, :, 2]
    
    return {
        "average_delta_e": float(delta_e),
        "sat_mean_render": float(np.mean(sat2)),
        "brightness_mean_render": float(np.mean(val2))
    }

# =====================================================================
# 6. GRADIENT SMOOTHNESS & BANDING TESTS
# =====================================================================
def run_gradient_tests(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    h, w = gray.shape
    
    # Analyze brightness profile along diagonal
    diagonal_profile = [gray[int(i * (h-1)/100), int(i * (w-1)/100)] for i in range(100)]
    diffs = np.diff(diagonal_profile)
    
    # Smoothness: variance of first derivatives. Banding: large abrupt jumps
    smoothness = float(np.var(diffs))
    banding_hits = int(np.sum(np.abs(diffs) > 30)) # Big jumps indicate step/banding artifacts
    
    return {
        "gradient_smoothness_variance": smoothness,
        "gradient_banding_pixel_jumps": banding_hits
    }

# =====================================================================
# 7. EDGE & SHAPE TESTS
# =====================================================================
def run_edge_tests(img1, img2):
    gray1 = cv2.GaussianBlur(cv2.cvtColor(img1, cv2.COLOR_BGR2GRAY), (5, 5), 0)
    gray2 = cv2.GaussianBlur(cv2.cvtColor(img2, cv2.COLOR_BGR2GRAY), (5, 5), 0)
    
    edges1 = cv2.Canny(gray1, 30, 120)
    edges2 = cv2.Canny(gray2, 30, 120)
    
    # Edge IoU with small dilation
    k = np.ones((3,3), np.uint8)
    d1 = cv2.dilate(edges1, k, iterations=1)
    d2 = cv2.dilate(edges2, k, iterations=1)
    
    inter = np.logical_and(d1 > 0, d2 > 0)
    union = np.logical_or(d1 > 0, d2 > 0)
    
    edge_iou = np.sum(inter) / float(np.sum(union)) if np.sum(union) > 0 else 1.0
    return {
        "edge_overlap_iou": float(edge_iou)
    }

# =====================================================================
# 8. SHADOW & GLOW DECAY PROFILING
# =====================================================================
def run_glow_tests(img):
    # Check the brightness falloff from the brightest regions
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    _, thresh = cv2.threshold(gray, 220, 255, cv2.THRESH_BINARY)
    
    # If no bright regions, no glow profile to analyze
    if np.sum(thresh) == 0:
        return {"glow_falloff_decay": 0.0}
        
    # Distance transform from brightest core
    dist = cv2.distanceTransform(cv2.bitwise_not(thresh), cv2.DIST_L2, 5)
    
    # Calculate average brightness at various distance thresholds
    decay_samples = []
    for d in [5, 10, 20, 40]:
        mask = (dist >= d) & (dist < d + 5)
        if np.sum(mask) > 0:
            decay_samples.append(np.mean(gray[mask]))
        else:
            decay_samples.append(0.0)
            
    # Calculate exponential decay coefficient
    x = np.array([5, 10, 20, 40])
    y = np.array(decay_samples)
    y = np.maximum(y, 1.0) # Avoid log(0)
    log_y = np.log(y)
    slope, _ = np.polyfit(x, log_y, 1)
    
    return {
        "glow_falloff_decay": float(slope)
    }

# =====================================================================
# 9. DETERMINISM TEST
# =====================================================================
def verify_determinism(executable, composition):
    print(f"[Determinism] Rendering '{composition}' twice for pixel-exact verification...")
    out1 = "output/det_temp_1.png"
    out2 = "output/det_temp_2.png"
    
    try:
        cmd1 = [executable, "render", composition, "-o", out1]
        cmd2 = [executable, "render", composition, "-o", out2]
        
        subprocess.run(cmd1, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(cmd2, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        with open(out1, "rb") as f1, open(out2, "rb") as f2:
            h1 = hashlib.sha256(f1.read()).hexdigest()
            h2 = hashlib.sha256(f2.read()).hexdigest()
            
        # Clean up
        if os.path.exists(out1): os.remove(out1)
        if os.path.exists(out2): os.remove(out2)
        
        return h1 == h2
    except Exception as e:
        print(f"[Determinism] Error during rendering run: {e}")
        return False

# =====================================================================
# 10. RESPONSIVE LAYOUT MARGIN CHECK
# =====================================================================
def verify_responsive_margins(render_path):
    print(f"[Safe Margins] Checking layout margins on '{render_path}'...")
    try:
        img = cv2.imread(render_path)
        if img is None:
            return False, "Failed to load render file for margin check"
            
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        h, w = gray.shape
        
        # Check outer 5% margin for visual overflows (e.g. text/graphic clipping)
        margin_w = int(w * 0.05)
        margin_h = int(h * 0.05)
        
        left_m = gray[:, :margin_w]
        right_m = gray[:, w - margin_w:]
        top_m = gray[:margin_h, :]
        bottom_m = gray[h - margin_h:, :]
        
        # High intensity on edge borders indicates clipping / margin overflow
        clipped_pixels = np.sum(left_m > 240) + np.sum(right_m > 240) + np.sum(top_m > 240) + np.sum(bottom_m > 240)
        
        if clipped_pixels > 20:
            return False, f"Detected element clipping ({clipped_pixels} high-intensity pixels in safe zone margins)"
            
        return True, "No margin clipping detected"
    except Exception as e:
        return False, f"Safe margins check error: {e}"

# =====================================================================
# MAIN RUNNER
# =====================================================================
def main():
    parser = argparse.ArgumentParser(description="Chronon3d Full Visual Quality Test Suite")
    parser.add_argument("--reference", help="Path to reference target image")
    parser.add_argument("--render", help="Path to rendered output image")
    parser.add_argument("--executable", default="./build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli", help="Path to chronon3d_cli")
    parser.add_argument("--composition", default="PremiumThumbnailSaaSBlue", help="Composition name to test determinism / responsive")
    parser.add_argument("--json", action="store_true", help="Print result as json")
    
    args = parser.parse_args()
    
    results = {}
    
    # 1. Image Comparability metrics
    if args.reference and args.render:
        if not os.path.exists(args.reference) or not os.path.exists(args.render):
            print("Error: Image files missing.", file=sys.stderr)
            sys.exit(1)
            
        img1 = cv2.imread(args.reference)
        img2 = cv2.imread(args.render)
        
        # Normalize size to match reference
        if img1.shape != img2.shape:
            img2 = cv2.resize(img2, (img1.shape[1], img1.shape[0]))
            
        pixel_res = run_pixel_tests(img1, img2)
        perceptual_res = run_perceptual_tests(img1, img2)
        layout_res = run_layout_tests(img1, img2)
        comp_res = run_composition_tests(img2)
        color_res = run_color_tests(img1, img2)
        gradient_res = run_gradient_tests(img2)
        edge_res = run_edge_tests(img1, img2)
        glow_res = run_glow_tests(img2)
        
        results.update({
            "pixel": pixel_res,
            "perceptual": perceptual_res,
            "layout": layout_res,
            "composition": comp_res,
            "color": color_res,
            "gradient": gradient_res,
            "edge": edge_res,
            "glow": glow_res
        })
        
    # 2. Functional Render & Pipeline Checks
    if os.path.exists(args.executable):
        det_ok = verify_determinism(args.executable, args.composition)
        render_path_to_check = args.render if args.render else "output/premium_thumbnail_saas_blue.png"
        resp_ok, resp_msg = verify_responsive_margins(render_path_to_check)
        results["pipeline"] = {
            "determinism_pass": det_ok,
            "responsive_layout_pass": resp_ok,
            "responsive_layout_message": resp_msg
        }
    else:
        results["pipeline"] = {
            "determinism_pass": False,
            "responsive_layout_pass": False,
            "responsive_layout_message": "Executable not found"
        }
        
    # Print results
    if args.json:
        print(json.dumps(results, indent=2))
    else:
        print("\n" + "="*50)
        print("         CHRONON3D VISUAL QUALITY SUITE")
        print("="*50)
        
        if "pixel" in results:
            print("\n[Pixel Metrics]")
            print(f"  MSE:               {results['pixel']['mse']:.3f}")
            print(f"  PSNR:              {results['pixel']['psnr']:.2f} dB")
            print(f"  Max Pixel Error:   {results['pixel']['max_pixel_error']}")
            
            print("\n[Perceptual Metrics]")
            print(f"  SSIM:              {results['perceptual']['ssim']:.4f}")
            print(f"  MS-SSIM:           {results['perceptual']['ms_ssim']:.4f}")
            
            print("\n[Layout Metrics]")
            print(f"  OCR Bbox IoU:      {results['layout']['ocr_layout_iou']:.4f}")
            print(f"  Center Error Px:   {results['layout']['center_error_px']:.2f} px")
            print(f"  Scale Error Ratio: {results['layout']['scale_error']:.4f}")
            
            print("\n[Color & Gradient Metrics]")
            print(f"  Lab Delta E:       {results['color']['average_delta_e']:.2f}")
            print(f"  Grad Banding Hits: {results['gradient']['gradient_banding_pixel_jumps']}")
            
            print("\n[Glow & Edge Metrics]")
            print(f"  Glow Falloff Coeff:{results['glow']['glow_falloff_decay']:.4f}")
            print(f"  Edge Overlap IoU:  {results['edge']['edge_overlap_iou']:.4f}")
            
        print("\n[Pipeline & Engine Verification]")
        print(f"  Deterministic Render: {'PASS' if results['pipeline']['determinism_pass'] else 'FAIL'}")
        print(f"  Responsive Layout:    {'PASS' if results['pipeline']['responsive_layout_pass'] else 'FAIL'} ({results['pipeline']['responsive_layout_message']})")
        print("="*50 + "\n")

if __name__ == "__main__":
    main()
