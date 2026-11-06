#include "module_gate.h"
#include <algorithm>

namespace HealthyModuleIdentity {
namespace {
void append64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
  for (int shift = 0; shift < 64; shift += 8) bytes.push_back(std::uint8_t(value >> shift));
}
bool read64(const std::vector<std::uint8_t>& bytes, std::size_t& index, std::uint64_t& value)
{
  if (index > bytes.size() || bytes.size() - index < 8) return false;
  value = 0;
  for (int shift = 0; shift < 64; shift += 8) value |= std::uint64_t(bytes[index++]) << shift;
  return true;
}
bool valid(const LaunchIdentity& value)
{
  return value.launch && value.size && value.file.volume && value.file.file != std::array<std::uint8_t, 16>{} &&
         value.digest == expectedDigest() && !value.canonicalPath.empty() && value.canonicalPath.size() <= 1024 &&
         value.canonicalPath.find(u'\0') == std::u16string::npos;
}
}

bool encodeIdentity(const FileObservation& observation, std::uint64_t launch, std::vector<std::uint8_t>& output)
{
  try {
  const LaunchIdentity value{launch, observation.size, observation.identity, observation.digest, observation.canonicalPath};
  if (!observation.pathComplete || !observation.identityComplete || !observation.hashComplete || !valid(value)) return false;
  std::vector<std::uint8_t> bytes{'H', 'M', 'I', '1'};
  append64(bytes, value.launch); append64(bytes, value.size); append64(bytes, value.file.volume);
  bytes.insert(bytes.end(), value.file.file.begin(), value.file.file.end());
  bytes.insert(bytes.end(), value.digest.begin(), value.digest.end());
  const auto count = std::uint16_t(value.canonicalPath.size());
  bytes.push_back(std::uint8_t(count)); bytes.push_back(std::uint8_t(count >> 8));
  for (char16_t character : value.canonicalPath) {
    bytes.push_back(std::uint8_t(character)); bytes.push_back(std::uint8_t(character >> 8));
  }
  if (bytes.size() > MaxIdentityFrame) return false;
  output = std::move(bytes);
  return true;
  } catch (...) { return false; }
}

bool decodeIdentity(const std::vector<std::uint8_t>& input, LaunchIdentity& output)
{
  try {
  if (input.size() < 78 || input.size() > MaxIdentityFrame ||
      !std::equal(input.begin(), input.begin() + 4, "HMI1")) return false;
  LaunchIdentity value;
  std::size_t index = 4;
  if (!read64(input, index, value.launch) || !read64(input, index, value.size) ||
      !read64(input, index, value.file.volume)) return false;
  std::copy_n(input.begin() + index, value.file.file.size(), value.file.file.begin()); index += value.file.file.size();
  std::copy_n(input.begin() + index, value.digest.size(), value.digest.begin()); index += value.digest.size();
  const auto count = std::uint16_t(input[index] | (std::uint16_t(input[index + 1]) << 8)); index += 2;
  if (!count || count > 1024 || input.size() - index != std::size_t(count) * 2) return false;
  value.canonicalPath.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    value.canonicalPath.push_back(char16_t(input[index] | (std::uint16_t(input[index + 1]) << 8)));
    index += 2;
  }
  if (!valid(value)) return false;
  output = std::move(value);
  return true;
  } catch (...) { return false; }
}

bool frameIdentity(const FileObservation& observation, std::uint64_t launch, std::vector<std::uint8_t>& output)
{
  try {
    std::vector<std::uint8_t> body;
    if (!encodeIdentity(observation, launch, body) || body.size() + 4 > MaxIdentityFrame) return false;
    const auto size = std::uint32_t(body.size());
    std::vector<std::uint8_t> frame{std::uint8_t(size >> 24), std::uint8_t(size >> 16),
                                    std::uint8_t(size >> 8), std::uint8_t(size)};
    frame.insert(frame.end(), body.begin(), body.end());
    output = std::move(frame);
    return true;
  } catch (...) { return false; }
}
}
