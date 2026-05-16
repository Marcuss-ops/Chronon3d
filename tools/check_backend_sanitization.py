import os
import sys
import subprocess

def check_no_legacy_files():
    legacy_files = [
        "src/backends/software/software_node_dispatch.cpp",
        "src/backends/software/software_renderer_draw_fallback.cpp"
    ]
    found = False
    for f in legacy_files:
        if os.path.exists(f):
            print(f"ERROR: Legacy file found: {f}")
            found = True
    return not found

def check_no_legacy_references():
    patterns = [
        "software_internal::draw_node",
    ]
    found_issue = False
    
    # Search in src and include
    search_dirs = ["src/backends/software", "include/chronon3d/backends/software"]
    
    for d in search_dirs:
        if not os.path.exists(d): continue
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith((".cpp", ".hpp")):
                    path = os.path.join(root, file)
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                        for i, line in enumerate(lines):
                            for p in patterns:
                                if p in line and "SoftwareNodeDispatcher::draw_node" not in line:
                                    print(f"ERROR: Legacy pattern '{p}' found in {path}:{i+1}")
                                    found_issue = True
    return not found_issue

def check_no_debug_smoke_signals():
    # Only check in software backend
    patterns = ["std::abort", "printf(", "std::cout", "std::cerr"]
    found_issue = False
    
    search_dirs = ["src/backends/software", "include/chronon3d/backends/software"]
    
    # We allow cerr for the SoftwareNodeDispatcher error logging we just added
    allowed_exceptions = {
        "src/backends/software/software_node_dispatcher.cpp": ["std::cerr"]
    }

    for d in search_dirs:
        if not os.path.exists(d): continue
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith((".cpp", ".hpp")):
                    path = os.path.normpath(os.path.join(root, file))
                    rel_path = os.path.relpath(path, ".").replace("\\", "/")
                    
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                        for i, line in enumerate(lines):
                            for p in patterns:
                                if p in line:
                                    # Check if this is an allowed exception
                                    is_allowed = False
                                    if rel_path in allowed_exceptions:
                                        if any(exc in p for exc in allowed_exceptions[rel_path]):
                                            is_allowed = True
                                    
                                    if not is_allowed:
                                        print(f"ERROR: Debug smoke signal '{p}' found in {path}:{i+1}")
                                        found_issue = True
    return not found_issue

if __name__ == "__main__":
    ok = True
    print("--- Running Sanitization Checks ---")
    if not check_no_legacy_files(): ok = False
    if not check_no_legacy_references(): ok = False
    if not check_no_debug_smoke_signals(): ok = False
    
    if ok:
        print("SUCCESS: All sanitization checks passed!")
        sys.exit(0)
    else:
        print("FAILURE: Sanitization checks failed.")
        sys.exit(1)
