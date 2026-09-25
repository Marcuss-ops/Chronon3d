#!/usr/bin/env python3
"""C++ <-> GLSL layout contract for the text-run glyph instance.

`text_run.comp` and `chronon3d::runtime::GlyphInstance` describe the same bytes
in two representations.  The C++ side is guarded by a `static_assert`, but that
guard is one-sided: a change to the GLSL struct breaks nothing on the C++ side
(the shared comment stayed at "40 bytes" while the C++ struct was already 48).
This test closes the loop with three independent checks:

  1. scalar-slot sequence  — GLSL members, expanded to scalar slots, must match
     the C++ field types in order (catches reorder / retype / add / remove);
  2. size                  — the std430 size computed from the GLSL members must
     equal the value pinned by the C++ `static_assert` (catches padding drift);
  3. compiled stride       — the `ArrayStride` that `glslangValidator` emits for
     the backing runtime array must equal the same number (catches layout what
     hand-written rules would get wrong).

Check 3 needs `glslangValidator` on PATH; when it is absent the test reports the
skip explicitly instead of silently passing.  Pure stdlib, runs on Python 3.9+.

Scope: the existing shader ABI suite (`tests/shader_abi_tests.cmake` →
`tests/backends/test_shader_abi_consistency.cpp`) reflects the compiled SPIR-V
for bindings and local sizes, but `tools/shaders/spirv_abi_gen.cpp` emits no
storage-buffer struct layout — and that suite needs Vulkan + spirv-reflect.  This
test covers the struct layout on any host; it should be absorbed by
`spirv_abi_gen` once that generator emits member offsets.

Wired from `tests/CMakeLists.txt` like the other report-driven python tests
(`tests/test_common_performance_gate.py`, `tests/test_gpu_corpus_manifest.py`).
"""

import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHADER = ROOT / "src" / "backends" / "vulkan" / "shaders" / "text_run.comp"
CPP_HEADER = ROOT / "include" / "chronon3d" / "runtime" / "render_surface.hpp"
STRUCT_NAME = "GlyphInstance"
TEXT_RUN_DYNAMIC = "TextRunDynamic"
TEXT_TILE_SHADERS = (
    ROOT / "src" / "backends" / "vulkan" / "shaders" / "text_tile_bin.comp",
    ROOT / "src" / "backends" / "vulkan" / "shaders" / "text_tile_raster.comp",
)

# GLSL scalar -> (canonical slot name, std430 size).
GLSL_SCALARS = {
    "float": ("f32", 4),
    "int": ("i32", 4),
    "uint": ("u32", 4),
}
# vecN family prefixes: "" -> float, "i" -> int, "u" -> uint.
GLSL_VECTOR = re.compile(r"^(i?u?)vec([234])$")

# C++ field type -> canonical slot name.
CPP_SCALARS = {
    "float": "f32",
    "double": "f64",
    "std::int32_t": "i32",
    "std::uint32_t": "u32",
    "std::int64_t": "i64",
    "std::uint64_t": "u64",
    "int": "i32",
    "unsigned": "u32",
}


def _read(path):
    if not path.exists():
        raise AssertionError("expected file is missing: %s" % path)
    return path.read_text(encoding="utf-8")


def _strip_line_comments(text):
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def _struct_body(text, name):
    """Return the raw body of `struct <name> { ... }` (first match).

    Brace-depth scan rather than a regex: member default initialisers like
    `{0}` would otherwise close a non-greedy `.*?}` match early.
    """
    text = _strip_line_comments(text)
    match = re.search(
        r"\bstruct\s+(?:alignas\s*\([^)]*\)\s*)?%s\s*\{" % re.escape(name),
        text,
    )
    if not match:
        raise AssertionError("struct %s not found" % name)
    start = match.end()
    depth = 1
    index = start
    while index < len(text) and depth:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    if depth:
        raise AssertionError("unterminated struct %s" % name)
    return text[start : index - 1]


def _glsl_members(body):
    """Parse GLSL members into [(name, scalar slots, std430 align, size)].

    A vector is one member with one alignment: `ivec2` is 6 int32 scalar slots
    spread over 3 members, not 6 independently 8-aligned slots.
    """
    members = []
    for raw in body.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "[" in line or "layout" in line:
            raise AssertionError(
                "unsupported GLSL member (arrays / layout qualifiers need this "
                "test extended): %r" % line
            )
        match = re.fullmatch(
            r"(?P<type>[A-Za-z_][\w]*)\s+(?P<name>[A-Za-z_]\w*)\s*;?", line
        )
        if not match:
            raise AssertionError("unparsable GLSL member: %r" % line)
        glsl_type = match.group("type")
        name = match.group("name")
        if glsl_type in GLSL_SCALARS:
            slot, size = GLSL_SCALARS[glsl_type]
            members.append((name, (slot,), size, size))
            continue
        vector = GLSL_VECTOR.match(glsl_type)
        if not vector:
            raise AssertionError(
                "unsupported GLSL type %r — extend GLSL_SCALARS/GLSL_VECTOR" % glsl_type
            )
        prefix, width = vector.group(1), int(vector.group(2))
        base = {"": "float", "i": "int", "u": "uint"}[prefix]
        slot, scalar_size = GLSL_SCALARS[base]
        # std430: vec2 aligns to 2 scalars, vec3/vec4 to 4.
        align = scalar_size * (2 if width == 2 else 4)
        members.append((name, tuple([slot] * width), align, scalar_size * width))
    if not members:
        raise AssertionError("no members parsed for %s" % STRUCT_NAME)
    return members


def _glsl_slots(members):
    """Flatten parsed members into their scalar slot sequence."""
    return [slot for _, slots, _, _ in members for slot in slots]


def _std430_size(members):
    """std430 size of a struct made of the given members."""
    size = 0
    struct_align = 1
    for _, _, align, member_size in members:
        size += (-size) % align
        size += member_size
        struct_align = max(struct_align, align)
    return size + ((-size) % struct_align)


def _cpp_fields(body):
    """C++ members as [(name, canonical slot)], in declaration order."""
    fields = []
    for raw in body.splitlines():
        line = raw.strip()
        if not line:
            continue
        match = re.fullmatch(
            r"(?P<type>[A-Za-z_:][\w:]*)\s+(?P<name>[A-Za-z_]\w*)"
            r"(?:\s*\{[^}]*\})?\s*;?",
            line,
        )
        if not match:
            raise AssertionError("unparsable C++ member: %r" % line)
        cpp_type = match.group("type")
        if cpp_type not in CPP_SCALARS:
            raise AssertionError(
                "unsupported C++ type %r — extend CPP_SCALARS" % cpp_type
            )
        fields.append((match.group("name"), CPP_SCALARS[cpp_type]))
    if not fields:
        raise AssertionError("no members parsed for %s" % STRUCT_NAME)
    return fields


def _cpp_pinned_size(header_text, struct_name=STRUCT_NAME):
    match = re.search(
        r"static_assert\(\s*sizeof\(\s*%s\s*\)\s*==\s*(?P<size>\d+)" % re.escape(struct_name),
        header_text,
    )
    if not match:
        raise AssertionError(
            "no static_assert(sizeof(%s) == ...) in %s — the C++ side of the "
            "contract is unguarded" % (STRUCT_NAME, CPP_HEADER)
        )
    return int(match.group("size"))


def _comment_declared_size(shader_text):
    """Byte count the shared comment claims, if any."""
    for line in shader_text.splitlines():
        if STRUCT_NAME in line and "bytes" in line:
            match = re.search(r"\((\d+)\s+bytes", line)
            if match:
                return int(match.group(1))
    return None


def _glsl_members_from_shader():
    return _glsl_members(_struct_body(_read(SHADER), STRUCT_NAME))


def _cpp_fields_from_header():
    return _cpp_fields(_struct_body(_read(CPP_HEADER), STRUCT_NAME))


def test_glsl_slot_sequence_matches_cpp():
    glsl = _glsl_slots(_glsl_members_from_shader())
    cpp = [slot for _, slot in _cpp_fields_from_header()]
    assert glsl == cpp, (
        "GLSL slots %r != C++ fields %r — the two layouts have diverged"
        % (glsl, cpp)
    )


def test_shared_member_names_keep_order():
    """Same-width members can be swapped without changing a byte of layout.

    The slot sequence cannot see that, so the members that keep the same name on
    both sides are used as anchors: if `opacity` and `scale_y` trade places, the
    shared-name subsequence changes order even though the size is identical.
    Members whose names are renamed across the boundary (`dst_origin` vs
    `dst_x`/`dst_y`, `glyph_size` vs `width`/`height`) carry no anchor and are
    only covered by the type/size checks above.
    """
    glsl_names = [name for name, _, _, _ in _glsl_members_from_shader()]
    cpp_names = [name for name, _ in _cpp_fields_from_header()]
    shared = [(name, cpp_names.index(name)) for name in glsl_names if name in cpp_names]
    assert shared, (
        "no member name is shared between GLSL and C++ — this check is vacuous, "
        "add an anchor member or delete the check"
    )
    positions = [position for _, position in shared]
    assert positions == sorted(positions), (
        "shared member names changed relative order: %r" % (shared,)
    )


def test_computed_size_matches_cpp_static_assert():
    computed = _std430_size(_glsl_members_from_shader())
    pinned = _cpp_pinned_size(_read(CPP_HEADER))
    assert computed == pinned, (
        "GLSL std430 size %d != C++ static_assert %d" % (computed, pinned)
    )


def test_shader_comment_declares_real_size():
    declared = _comment_declared_size(_read(SHADER))
    assert declared is not None, (
        "text_run.comp no longer states the byte count — the comment is the only "
        "cross-file breadcrumb for the GLSL side"
    )
    computed = _std430_size(_glsl_members_from_shader())
    assert declared == computed, (
        "text_run.comp comment says %d bytes, the layout is %d" % (declared, computed)
    )


def _spirv_array_stride(shader=SHADER, struct_name=STRUCT_NAME):
    """ArrayStride emitted for the runtime array whose element is struct_name."""
    tool = shutil.which("glslangValidator")
    if not tool:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / (shader.stem + ".spv")
        proc = subprocess.run(
            [tool, "-V", "-g", str(shader), "-o", str(out)],
            capture_output=True,
            text=True,
        )
        if proc.returncode != 0 or not out.exists():
            raise AssertionError(
                "glslangValidator failed on %s: %s" % (shader.name, proc.stderr.strip())
            )
        data = out.read_bytes()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    op_name, op_type_runtime_array, op_decorate, dec_array_stride = 5, 29, 71, 6
    names, runtime_arrays, strides = {}, {}, {}
    index = 5  # skip the 5-word header
    while index < len(words):
        word_count = words[index] >> 16
        opcode = words[index] & 0xFFFF
        if word_count == 0 or index + word_count > len(words):
            break
        if opcode == op_name:
            raw_name = struct.pack(
                "<%dI" % (word_count - 2), *words[index + 2 : index + word_count]
            ).split(bytes([0]), 1)[0].decode("utf-8")
            names[raw_name] = words[index + 1]
        elif opcode == op_type_runtime_array:
            runtime_arrays[words[index + 1]] = words[index + 2]
        elif opcode == op_decorate and words[index + 2] == dec_array_stride:
            strides[words[index + 1]] = words[index + 3]
        index += word_count
    struct_id = names.get(struct_name)
    assert struct_id is not None, (
        "glslangValidator emitted no OpName for %s in %s"
        % (struct_name, shader.name)
    )
    matching_arrays = [
        array_id for array_id, element_id in runtime_arrays.items()
        if element_id == struct_id
    ]
    assert len(matching_arrays) == 1, (
        "expected one runtime array of %s in %s, found %d"
        % (struct_name, shader.name, len(matching_arrays))
    )
    array_id = matching_arrays[0]
    assert array_id in strides, (
        "glslangValidator emitted no ArrayStride for %s in %s"
        % (struct_name, shader.name)
    )
    return strides[array_id]


def test_compiled_array_stride_matches_cpp():
    stride = _spirv_array_stride(SHADER, STRUCT_NAME)
    if stride is None:
        print("  SKIP: glslangValidator not on PATH — compiled stride not checked")
        return False
    pinned = _cpp_pinned_size(_read(CPP_HEADER))
    assert stride == pinned, (
        "compiled ArrayStride %d != C++ static_assert %d" % (stride, pinned)
    )
    return True


def test_text_tile_dynamic_layout_matches_cpp():
    header = _read(CPP_HEADER)
    cpp_body = _struct_body(header, TEXT_RUN_DYNAMIC)
    cpp_fields = _cpp_fields(cpp_body)
    pinned = _cpp_pinned_size(header, TEXT_RUN_DYNAMIC)
    assert pinned == 128, "TextRunDynamic ABI must remain 128 bytes"

    for shader in TEXT_TILE_SHADERS:
        shader_body = _struct_body(_read(shader), "GpuTextRunDynamic")
        glsl_members = _glsl_members(shader_body)
        glsl_fields = [
            (name, slot)
            for name, slots, _, _ in glsl_members
            for slot in slots
            if len(slots) == 1
        ]
        assert glsl_fields == cpp_fields, (
            "%s GpuTextRunDynamic fields diverge from C++ TextRunDynamic: "
            "%r != %r" % (shader.name, glsl_fields, cpp_fields)
        )
        computed = _std430_size(glsl_members)
        assert computed == pinned, (
            "%s std430 size %d != C++ TextRunDynamic size %d"
            % (shader.name, computed, pinned)
        )
        stride = _spirv_array_stride(shader, "GpuTextRunDynamic")
        if stride is not None:
            assert stride == pinned, (
                "%s compiled TextRunDynamic ArrayStride %d != C++ size %d"
                % (shader.name, stride, pinned)
            )


if __name__ == "__main__":
    test_glsl_slot_sequence_matches_cpp()
    test_shared_member_names_keep_order()
    test_computed_size_matches_cpp_static_assert()
    test_shader_comment_declares_real_size()
    compiled = test_compiled_array_stride_matches_cpp()
    test_text_tile_dynamic_layout_matches_cpp()
    print(
        "shader layout contract tests: PASS (text-run SPIR-V stride check: %s; "
        "text-tile GLSL/C++ layout checks: PASS)"
        % ("ran" if compiled else "skipped")
    )
    sys.exit(0)
