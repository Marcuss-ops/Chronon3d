#!/usr/bin/env python3
"""Alphabetize Chronon3DPublicHeaders.cmake into a single canonical-bucket-
sorted list.  Module-bucket rule strips the leading 'chronon3d/internal/'
prefix so internal/* sits with its public module peer (existing-bucket
convention).  Preserves audit comments attached to specific entries
(only `detail/framebuffer_impl.hpp` carries one today) and strips the
now-resolved TICKET-GATE-10-PHASE-4 comment block.
"""
import re, pathlib

P = pathlib.Path("cmake/Chronon3DPublicHeaders.cmake")
lines = P.read_text().splitlines(keepends=True)

BULK_MARK = "TICKET-GATE-10-PHASE-4"          # ASCII marker
HEAD_MARK = "timeline/composition.hpp"

bulk_idx = close_idx = head_end_idx = None
for i, ln in enumerate(lines):
    if bulk_idx is None and BULK_MARK in ln:
        bulk_idx = i
    if head_end_idx is None and HEAD_MARK in ln:
        head_end_idx = i + 1
    if bulk_idx is not None and i > bulk_idx and ln.strip() == ")" and close_idx is None:
        close_idx = i
assert all(v is not None for v in (bulk_idx, close_idx, head_end_idx)), \
    f"parse fail: head_end={head_end_idx} bulk={bulk_idx} close={close_idx}"

# Existing entries (with optional preceding comment attached to next path entry)
existing = []
prev_comment = []
for ln in lines[head_end_idx:bulk_idx]:
    s = ln.strip()
    m = re.match(r'"\$\{CMAKE_SOURCE_DIR\}/include/(chronon3d/[^"]+)"', s)
    if m:
        existing.append({"pre_comment": list(prev_comment), "path": m.group(1)})
        prev_comment = []
    elif s == "" or not s.startswith("#"):
        prev_comment = []
    else:
        prev_comment.append(ln)

bulk = []
for i in range(bulk_idx, close_idx):
    s = lines[i].strip()
    m = re.match(r'"\$\{CMAKE_SOURCE_DIR\}/include/(chronon3d/[^"]+)"', s)
    if m:
        bulk.append({"pre_comment": [], "path": m.group(1)})

n_existing = len(existing)
n_bulk     = len(bulk)
n_total    = n_existing + n_bulk
n_unique   = len({it["path"] for it in existing + bulk})
print(f"existing: {n_existing}  bulk: {n_bulk}  total: {n_total}  unique: {n_unique}")
assert n_unique == n_total, f"pre-merge duplicates: total={n_total} unique={n_unique}"

# Sanity: framebuffer_impl.hpp must carry the 3-line audit-trail comment
fb_present = False
for it in existing:
    if "framebuffer_impl.hpp" in it["path"]:
        assert len(it["pre_comment"]) == 3, \
            f"framebuffer_impl lost pre_comment ({len(it['pre_comment'])} lines)"
        fb_present = True
assert fb_present, "framebuffer_impl.hpp not found in existing list"

# Module-bucket sort: strip internal/ prefix
def mk(rel):
    rest = rel[len("chronon3d/"):]
    if rest.startswith("internal/"):
        rest = rest[len("internal/"):]
    return rest

all_items = sorted(existing + bulk, key=lambda it: (mk(it["path"]), it["path"]))

new = list(lines[:head_end_idx])
for it in all_items:
    if it["pre_comment"]:
        new.extend(it["pre_comment"])
    new.append('    "${CMAKE_SOURCE_DIR}/include/' + it["path"] + '"\n')
new.extend(lines[close_idx:])

new_str = "".join(new)

# Strip the resolved bulk-comment block (bulk marker -> next path line)
start = new_str.find(BULK_MARK)
if start >= 0:
    m = re.search(
        r'    "\$\{CMAKE_SOURCE_DIR\}/include/chronon3d/',
        new_str[start:],
    )
    if m:
        first_entry = start + m.start()
        prev_blank = new_str.rfind("\n\n", 0, first_entry)
        if prev_blank > 0:
            new_str = new_str[:prev_blank] + new_str[first_entry:]

P.write_text(new_str)

# Verify final
written_lines = P.read_text().splitlines()
final_paths = []
for ln in written_lines:
    m = re.match(r'\s*"\${CMAKE_SOURCE_DIR}/include/(chronon3d/[^"]+)"', ln)
    if m:
        final_paths.append(m.group(1))
assert len(final_paths) == n_total, \
    f"final != pre-merge total: {len(final_paths)} vs {n_total}"
assert len(set(final_paths)) == n_total, "duplicate detected"
assert BULK_MARK not in P.read_text(), "bulk marker survived strip"
print(f"WROTE: {len(final_paths)} entries; framebuffer_impl comment preserved; bulk-comment stripped")
# Spot-check canonical-bucket order
for i, p in enumerate(final_paths):
    if i + 1 < len(final_paths):
        a, b = mk(p), mk(final_paths[i + 1])
        if a == b and p > final_paths[i + 1]:
            print(f"  OUT-OF-ORDER within bucket: {p} > {final_paths[i + 1]}")
        elif a > b:
            print(f"  OUT-OF-ORDER across buckets at {i}: {p} -> {final_paths[i + 1]}")
print("VERIFY_OK")
