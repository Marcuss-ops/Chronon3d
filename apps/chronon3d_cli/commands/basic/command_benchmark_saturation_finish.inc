out << "PARALLELISM\n";
out << "Workers (TBB arena)......... " << ctx.cpu_budget.render_threads << "\n";
out << "Tile size................... " << settings.dirty.tile_size << " (default: 0 = auto)\n";
out << "SIMD........................ " << simd::cpu_isa_name(caps.highest_isa) << "\n";
out << "\n";

out << "EFFICIENCY\n";
const bool cpu_sat = ctx.cpu_budget.render_threads >= static_cast<int>(logical_cpus) * 0.75;
out << "CPU saturation................ " << (cpu_sat ? "PASS" : "FAIL") << "\n";

const bool scheduler_ok = (perf.available == "true");
out << "Scheduler efficiency.......... " << (scheduler_ok ? "N/A (install perf for IPC)" : "N/A") << "\n";

const bool simd_ok = (caps.highest_isa != simd::CpuIsa::Scalar);
out << "SIMD utilization.............. " << (simd_ok ? "PASS" : "FAIL (scalar only)") << "\n";

const bool mem_ok = (alloc_per_frame == 0);
out << "Memory pressure............... " << (mem_ok ? "PASS" : fmt::format("FAIL ({} allocs/frame)", alloc_per_frame)) << "\n";
out << "Determinism.................. NOT RUN (use tools/check_determinism.sh)\n";
out << "\n";
out << "================================================\n";
out << "\n";

spdlog::info("Saturation report complete: {} frames in {:.1f}s, {:.1f} FPS",
              total_frames, total_elapsed_sec, fps);
return 0;
