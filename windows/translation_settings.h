#pragma once

#include "server/subtitle_server.h"

#include <filesystem>

namespace overlay_trans {

std::filesystem::path translation_settings_path();
TranslationSettings load_translation_settings();
bool save_translation_settings(const TranslationSettings& settings);

}  // namespace overlay_trans
