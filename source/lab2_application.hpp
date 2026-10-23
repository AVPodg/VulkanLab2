#pragma once
#include "application.hpp"
namespace lab2 {
bool initialize(const std::filesystem::path& directory);
void shutdown();
void update(double time);
void render(const graphics::internal::FrameData& frame);
}
