DoctorReport run_doctor(const DoctorOptions& options) {
    DoctorReport report;
    collect_engine_checks(report);
    collect_system_checks(report);
    collect_backend_checks(report);
    collect_encoder_checks(report);
    collect_asset_checks(report, options);
    collect_filesystem_checks(report);
    collect_deep_check(report, options);

    report.ready = true;
    for (const auto& check : report.checks) {
        if (check.status == DoctorStatus::Fail) {
            report.ready = false;
            break;
        }
    }
    return report;
}

const char* doctor_status_name(DoctorStatus status) noexcept {
    return doctor_status_name_impl(status);
}

const char* doctor_status_label(DoctorStatus status) noexcept {
    return doctor_status_label_impl(status);
}
