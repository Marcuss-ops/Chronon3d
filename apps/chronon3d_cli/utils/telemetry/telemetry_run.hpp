#pragma once

// Backward-compatibility forwarding header.
//
// telemetry_run.hpp has been split into three focused modules:
//   telemetry_capture.hpp   - capture_counters(), capture_graph_phase_records()
//   telemetry_merge.hpp     - add_counters()
//   telemetry_record.hpp    - populate_run_metrics(), record_output_run()
//
// Prefer including the specific header you need.

#include "telemetry_capture.hpp"
#include "telemetry_merge.hpp"
#include "telemetry_record.hpp"

