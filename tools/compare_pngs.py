import os
import hashlib
import sys
from PIL import Image, ImageChops

def get_hash(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()

def compare_pixels(path1, path2):
    img1 = Image.open(path1).convert("RGBA")
    img2 = Image.open(path2).convert("RGBA")
    if img1.size != img2.size:
        return False, f"Size mismatch: {img1.size} vs {img2.size}"
    
    diff = ImageChops.difference(img1, img2)
    if diff.getbbox():
        return False, "Pixels differ"
    return True, "Identical"

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 compare_pngs.py <dir1> <dir2>")
        sys.exit(1)

    dir1 = sys.argv[1]
    dir2 = sys.argv[2]

    mismatches = []
    total = 0
    identical = 0

    for root, _, files in os.walk(dir1):
        for f in files:
            if not f.endswith(".png"): continue
            total += 1
            p1 = os.path.join(root, f)
            rel = os.path.relpath(p1, dir1)
            p2 = os.path.join(dir2, rel)

            if not os.path.exists(p2):
                print(f"[MISSING] {rel}")
                mismatches.append(rel)
                continue

            h1 = get_hash(p1)
            h2 = get_hash(p2)

            if h1 == h2:
                identical += 1
                # print(f"[OK] {rel}")
            else:
                ok, msg = compare_pixels(p1, p2)
                if ok:
                    identical += 1
                    # print(f"[OK] {rel} (metadata differ, pixels identical)")
                else:
                    print(f"[FAIL] {rel}: {msg}")
                    mismatches.append(rel)

    print("-" * 40)
    print(f"Total: {total}")
    print(f"Identical: {identical}")
    print(f"Mismatches: {len(mismatches)}")
    
    if mismatches:
        sys.exit(1)
    else:
        print("Verification SUCCESS: All proofs are bit-exact or pixel-identical!")
        sys.exit(0)

if __name__ == "__main__":
    main()
