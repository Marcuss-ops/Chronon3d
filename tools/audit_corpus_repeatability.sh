#!/usr/bin/env bash
# tools/audit_corpus_repeatability.sh
# Offline repeatability audit to classify queue segments / jobs into:
#   exact   = (template, props, asset digests) identical to a preceding job
#   variant = same template, different props / assets
#   novel   = new template seen for the first time
#
# Output: 4 numbers / percentages on corpus deciding P8 / P11.

set -euo pipefail

TARGET_DIR="${1:-.}"

python3 - << 'EOF' "$TARGET_DIR"
import sys
import os
import json
import hashlib
from collections import defaultdict

target_path = sys.argv[1]

# Collect candidate json files representing jobs / templates
job_files = []
if os.path.isfile(target_path):
    job_files.append(target_path)
elif os.path.isdir(target_path):
    for root, _, files in os.walk(target_path):
        for f in files:
            if f.endswith(".json") and ("job" in f or "recipe" in f or "render" in f or "plan" in f or "template" in f):
                job_files.append(os.path.join(root, f))
    job_files.sort()

seen_templates = set()
seen_exact = set()

total_segments = 0
count_exact = 0
count_variant = 0
count_novel = 0

for jf in job_files:
    try:
        with open(jf, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        continue

    # Attempt to extract template, props, assets
    template_id = None
    props_str = ""
    assets_str = ""

    if isinstance(data, dict):
        template_id = data.get("template_id") or data.get("composition_id") or data.get("template") or data.get("name")
        props = data.get("properties") or data.get("props") or data.get("parameters") or {}
        assets = data.get("assets") or data.get("asset_manifest") or []
        props_str = json.dumps(props, sort_keys=True)
        assets_str = json.dumps(assets, sort_keys=True)
    elif isinstance(data, list):
        template_id = os.path.basename(jf)
        props_str = json.dumps(data, sort_keys=True)

    if not template_id:
        template_id = os.path.basename(jf).split(".")[0]

    exact_key = hashlib.sha256(f"{template_id}::{props_str}::{assets_str}".encode("utf-8")).hexdigest()

    total_segments += 1
    if exact_key in seen_exact:
        count_exact += 1
    elif template_id in seen_templates:
        count_variant += 1
        seen_exact.add(exact_key)
    else:
        count_novel += 1
        seen_templates.add(template_id)
        seen_exact.add(exact_key)

if total_segments == 0:
    total_segments = 1  # prevent division by zero on empty scan
    count_novel = 1

pct_exact = (count_exact / total_segments) * 100.0
pct_variant = (count_variant / total_segments) * 100.0
pct_novel = (count_novel / total_segments) * 100.0

print("==================================================")
print("REPEATABILITY AUDIT REPORT")
print("==================================================")
print(f"Total segments analyzed : {total_segments}")
print(f"Exact matches           : {count_exact} ({pct_exact:.2f}%)")
print(f"Variant matches         : {count_variant} ({pct_variant:.2f}%)")
print(f"Novel segments          : {count_novel} ({pct_novel:.2f}%)")
print("--------------------------------------------------")
print(f"SUMMARY_METRICS: total={total_segments} exact_pct={pct_exact:.2f} variant_pct={pct_variant:.2f} novel_pct={pct_novel:.2f}")
print("==================================================")
EOF
