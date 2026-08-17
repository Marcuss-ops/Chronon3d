#!/usr/bin/env python3
import importlib.util
from pathlib import Path

MODULE_PATH = Path(__file__).parents[1] / "tools" / "check_common_performance_gate.py"
spec = importlib.util.spec_from_file_location("common_gate", MODULE_PATH)
assert spec and spec.loader
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def report():
    return {
        "schema": "chronon3d.bench.v3",
        "render": {"frames": 10, "modular_graph": True},
        "metrics": {"p50_frame_ms": 1.0, "p95_frame_ms": 2.0, "p99_frame_ms": 3.0, "fps": 100.0},
        "frame_times_ms": [1.0, 2.0, 3.0],
        "memory": {"peak_rss_mb": 10.0, "peak_framebuffer_bytes": 20.0},
        "counters": {
            "pixels_touched": 100,
            "bytes_touched": 400,
            "full_frame_passes": 1,
            "full_frame_copies": 0,
            "conversion_ms": 0.5,
            "encoder_copy_bytes": 0,
            "cache_hit_rate": 0.75,
            "nodes_skipped": 2,
            "fused_passes": 1,
        },
        "quality": {"deterministic_hash": "0123456789abcdef"},
    }


def test_complete_contract_passes():
    assert module.validate(report()) == []


def test_missing_metric_fails_loudly():
    current = report()
    del current["counters"]["encoder_copy_bytes"]
    issues = module.validate(current)
    assert any("missing-required: counters.encoder_copy_bytes" in issue for issue in issues)


def test_invalid_percentile_order_fails():
    current = report()
    current["metrics"]["p99_frame_ms"] = 1.5
    assert any("percentile-order" in issue for issue in module.validate(current))


def test_runtime_aliases_are_normalized():
    current = report()
    del current["counters"]["conversion_ms"]
    current["counters"]["video_conversion_wall_ms"] = 0.5
    del current["memory"]["peak_framebuffer_bytes"]
    current["memory"]["framebuffer_bytes_peak"] = 20.0
    assert module.validate(current) == []
    assert current["counters"]["conversion_ms"] == 0.5
    assert current["memory"]["peak_framebuffer_bytes"] == 20.0


def test_percentiles_are_derived_when_missing():
    current = report()
    for key in ("p50_frame_ms", "p95_frame_ms", "p99_frame_ms", "fps"):
        del current["metrics"][key]
    assert module.validate(current) == []
    assert current["metrics"]["p50_frame_ms"] == 2.0
    assert current["metrics"]["p95_frame_ms"] > 2.0
    assert current["metrics"]["p99_frame_ms"] == 2.98


def test_bytes_touched_is_derived_from_read_write():
    current = report()
    del current["counters"]["bytes_touched"]
    current["counters"]["bytes_read"] = 100
    current["counters"]["bytes_written"] = 300
    assert module.validate(current) == []
    assert current["counters"]["bytes_touched"] == 400


def test_invalid_hash_fails():
    current = report()
    current["quality"]["deterministic_hash"] = "not-hex"
    assert any("invalid-output-hash" in issue for issue in module.validate(current))


def test_invalid_schema_fails():
    current = report()
    current["schema"] = "chronon3d.bench.v2"
    assert any("invalid-schema" in issue for issue in module.validate(current))


if __name__ == "__main__":
    test_complete_contract_passes()
    test_missing_metric_fails_loudly()
    test_invalid_percentile_order_fails()
    test_runtime_aliases_are_normalized()
    test_percentiles_are_derived_when_missing()
    test_invalid_hash_fails()
    test_invalid_schema_fails()
    test_bytes_touched_is_derived_from_read_write()
    print("common performance gate tests: PASS")
