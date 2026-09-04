namespace {

std::string lower_extension(const std::string& path) {
    std::filesystem::path p(path);
    std::string ext = p.extension().string();

    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return ext;
}

void ensure_parent_dir(const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
}

} // namespace

ImageFormat image_format_from_path(const std::string& path) {
    const std::string ext = lower_extension(path);

    if (ext == ".png") {
        return ImageFormat::Png;
    }

    if (ext == ".exr") {
        return ImageFormat::Exr;
    }

    return ImageFormat::Unknown;
}
