#pragma once

namespace chronon3d {

// The former scene/debug rendering interface was retired in Fase C3. Keep
// this lightweight type name for internal source compatibility; rendering now
// enters through the canonical graph or compiled-composition APIs.
class Renderer {
public:
    Renderer() = default;
    virtual ~Renderer() = default;
    Renderer(const Renderer&) = default;
    Renderer& operator=(const Renderer&) = default;
    Renderer(Renderer&&) noexcept = default;
    Renderer& operator=(Renderer&&) noexcept = default;

};

} // namespace chronon3d
