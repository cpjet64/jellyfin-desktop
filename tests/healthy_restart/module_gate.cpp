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
  const auto& binding = value.binding;
  const bool source = binding.role == LaunchRole::Source && binding.revision && binding.assetDigest != Digest{} && binding.expectedGrantDigest == Digest{};
  const bool restore = binding.role == LaunchRole::Restore && !binding.revision && binding.assetDigest == Digest{} && binding.expectedGrantDigest != Digest{};
  return binding.run && (source || restore) && value.launch && value.size && value.file.volume && value.file.file != std::array<std::uint8_t, 16>{} &&
         value.digest == expectedDigest() && !value.canonicalPath.empty() && value.canonicalPath.size() <= 1024 &&
         value.canonicalPath.find(u'\0') == std::u16string::npos;
}
}

bool encodeIdentity(const FileObservation& observation, std::uint64_t launch, const LaunchBinding& binding, std::vector<std::uint8_t>& output)
{
  try {
  const LaunchIdentity value{launch, observation.size, observation.identity, observation.digest, observation.canonicalPath, binding};
  if (!observation.pathComplete || !observation.identityComplete || !observation.hashComplete || !valid(value)) return false;
  std::vector<std::uint8_t> bytes{'H', 'M', 'I', '2'};
  append64(bytes, value.launch); append64(bytes, value.size); append64(bytes, value.file.volume);
  bytes.insert(bytes.end(), value.file.file.begin(), value.file.file.end());
  bytes.insert(bytes.end(), value.digest.begin(), value.digest.end());
  bytes.push_back(std::uint8_t(binding.role)); append64(bytes, binding.run);
  if (binding.role == LaunchRole::Source) append64(bytes, binding.revision);
  const auto& commitment = binding.role == LaunchRole::Source ? binding.assetDigest : binding.expectedGrantDigest;
  bytes.insert(bytes.end(), commitment.begin(), commitment.end());
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
  if (input.size() < 119 || input.size() > MaxIdentityFrame ||
      !std::equal(input.begin(), input.begin() + 4, "HMI2")) return false;
  LaunchIdentity value;
  std::size_t index = 4;
  if (!read64(input, index, value.launch) || !read64(input, index, value.size) ||
      !read64(input, index, value.file.volume)) return false;
  std::copy_n(input.begin() + index, value.file.file.size(), value.file.file.begin()); index += value.file.file.size();
  std::copy_n(input.begin() + index, value.digest.size(), value.digest.begin()); index += value.digest.size();
  value.binding.role = static_cast<LaunchRole>(input[index++]);
  if (!read64(input, index, value.binding.run)) return false;
  if (value.binding.role == LaunchRole::Source && !read64(input, index, value.binding.revision)) return false;
  if (input.size() - index < 34) return false;
  auto& commitment = value.binding.role == LaunchRole::Source ? value.binding.assetDigest : value.binding.expectedGrantDigest;
  std::copy_n(input.begin() + index, commitment.size(), commitment.begin()); index += commitment.size();
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

bool frameIdentity(const FileObservation& observation, std::uint64_t launch, const LaunchBinding& binding, std::vector<std::uint8_t>& output)
{
  try {
    std::vector<std::uint8_t> body;
    if (!encodeIdentity(observation, launch, binding, body) || body.size() + 4 > MaxIdentityFrame) return false;
    const auto size = std::uint32_t(body.size());
    std::vector<std::uint8_t> frame{std::uint8_t(size >> 24), std::uint8_t(size >> 16),
                                    std::uint8_t(size >> 8), std::uint8_t(size)};
    frame.insert(frame.end(), body.begin(), body.end());
    output = std::move(frame);
    return true;
  } catch (...) { return false; }
}
}
