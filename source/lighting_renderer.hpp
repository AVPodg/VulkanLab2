#pragma once
#include "graphics_internal.hpp"
#include "lighting_scene.hpp"
#include <filesystem>
namespace lighting {
void initializeRenderer(const std::filesystem::path& directory);
void shutdownRenderer();
void render(const graphics::internal::FrameData& frame, const State& state, float panelWidth);
}
