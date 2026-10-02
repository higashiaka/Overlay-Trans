#pragma once

#include <filesystem>
#include <string>

namespace overlay_trans {

// 확장 프로그램과 한 번만 페어링하면 되도록, 페어링 키를 파일에 저장해 두고 다시 쓴다.
// 파일이 없으면 무작위 키를 새로 만들어 저장한다. 저장에 실패하면 빈 문자열을 반환한다.
std::string load_or_create_pairing_token(const std::filesystem::path& file);

}  // namespace overlay_trans
