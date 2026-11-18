#include "AnimationAsset.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>

namespace sat {
namespace {
void put(std::string &bytes, std::uint32_t value, unsigned count = 4) {
  for (unsigned i = 0; i < count; ++i) {
    bytes.push_back(static_cast<char>((value >> (i * 8)) & 255));
  }
}
void putFloat(std::string &bytes, float value) {
  put(bytes, std::bit_cast<std::uint32_t>(value));
}
void putString(std::string &bytes, const std::string &value) {
  put(bytes, static_cast<std::uint32_t>(value.size()));
  bytes += value;
}
class Reader {
public:
  explicit Reader(std::string_view bytes) : m_bytes(bytes) {}
  bool integer(std::uint32_t &value, unsigned count = 4) {
    if (m_bytes.size() < count) {
      return false;
    }
    value = 0;
    for (unsigned i = 0; i < count; ++i) {
      value |=
          static_cast<std::uint32_t>(static_cast<unsigned char>(m_bytes[i]))
          << (8 * i);
    }
    m_bytes.remove_prefix(count);
    return true;
  }
  bool number(float &value) {
    std::uint32_t bits = 0;
    if (!integer(bits)) {
      return false;
    }
    value = std::bit_cast<float>(bits);
    return std::isfinite(value);
  }
  bool string(std::string &value) {
    std::uint32_t size = 0;
    if (!integer(size) || size > 4096 || size > m_bytes.size()) {
      return false;
    }
    value.assign(m_bytes.substr(0, size));
    m_bytes.remove_prefix(size);
    return value.find('\0') == std::string::npos;
  }
  std::size_t remaining() const { return m_bytes.size(); }

private:
  std::string_view m_bytes;
};
} // namespace
bool isAnimationClipAssetPath(std::string_view path) {
  if (!isValidAssetPath(path) || path.size() < 8) {
    return false;
  }
  auto extension = std::string(path.substr(path.size() - 8));
  for (char &ch : extension) {
    if (ch >= 'A' && ch <= 'Z') {
      ch += 'a' - 'A';
    }
  }
  return extension == ".satanim";
}
bool validateAnimationClip(const AnimationClipAsset &asset,
                           std::string &error) {
  const auto fail = [&](const char *message) {
    error = message;
    return false;
  };
  if (asset.clip.name.size() > 4096 ||
      asset.clip.name.find('\0') != std::string::npos ||
      !std::isfinite(asset.clip.duration) || asset.clip.duration < 0 ||
      !std::isfinite(asset.startSeconds) || !std::isfinite(asset.endSeconds) ||
      asset.startSeconds < 0 || asset.endSeconds < asset.startSeconds ||
      asset.endSeconds > asset.clip.duration || !std::isfinite(asset.speed) ||
      asset.jointNames.size() > kMaxAnimationJoints ||
      asset.clip.channels.size() > kMaxAnimationJoints * 3) {
    return fail("Invalid animation clip settings or counts");
  }
  std::unordered_set<std::string> names;
  for (const auto &name : asset.jointNames) {
    if (name.empty() || name.size() > 4096 ||
        name.find('\0') != std::string::npos || !names.insert(name).second) {
      return fail("Animation joint names must be nonempty and unique");
    }
  }
  std::unordered_set<unsigned> targets;
  std::size_t bytes = 0;
  for (const auto &channel : asset.clip.channels) {
    if (channel.joint >= asset.jointNames.size() ||
        channel.path > AnimationPath::Scale ||
        channel.interpolation > AnimationInterpolation::CubicSpline ||
        channel.times.empty() || channel.times.size() > kMaxAnimationKeys ||
        !targets
             .insert(channel.joint * 3u + static_cast<unsigned>(channel.path))
             .second) {
      return fail("Invalid or duplicate animation channel");
    }
    const std::size_t width = channel.path == AnimationPath::Rotation ? 4 : 3;
    const std::size_t multiplier =
        channel.interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
    if (channel.values.size() != channel.times.size() * width * multiplier) {
      return fail("Animation key count mismatch");
    }
    bytes += (channel.times.size() + channel.values.size()) * sizeof(float);
    if (bytes > kMaxAssetBytes) {
      return fail("Animation exceeds asset size limit");
    }
    float previous = -1;
    for (float time : channel.times) {
      if (!std::isfinite(time) || time < 0 || time <= previous ||
          time > asset.clip.duration) {
        return fail("Animation times must be finite and strictly increasing");
      }
      previous = time;
    }
    for (float value : channel.values) {
      if (!std::isfinite(value)) {
        return fail("Nonfinite animation value");
      }
    }
    if (width == 4) {
      for (std::size_t key = 0; key < channel.times.size(); ++key) {
        const auto offset =
            (key * multiplier + (multiplier == 3 ? 1 : 0)) * width;
        double norm = 0;
        for (std::size_t axis = 0; axis < 4; ++axis) {
          norm += double(channel.values[offset + axis]) *
                  channel.values[offset + axis];
        }
        if (norm < 1e-12) {
          return fail("Zero animation quaternion");
        }
      }
    }
  }
  error.clear();
  return true;
}
bool encodeAnimationClip(const AnimationClipAsset &asset, std::string &bytes,
                         std::string &error) {
  if (!validateAnimationClip(asset, error)) {
    return false;
  }
  std::string result = "SANM";
  put(result, 1);
  putString(result, asset.clip.name);
  putFloat(result, asset.clip.duration);
  putFloat(result, asset.startSeconds);
  putFloat(result, asset.endSeconds);
  put(result, asset.loop ? 1 : 0, 1);
  putFloat(result, asset.speed);
  put(result, static_cast<std::uint32_t>(asset.jointNames.size()));
  for (const auto &name : asset.jointNames) {
    putString(result, name);
  }
  put(result, static_cast<std::uint32_t>(asset.clip.channels.size()));
  for (const auto &channel : asset.clip.channels) {
    put(result, channel.joint, 2);
    put(result, static_cast<unsigned>(channel.path), 1);
    put(result, static_cast<unsigned>(channel.interpolation), 1);
    put(result, static_cast<std::uint32_t>(channel.times.size()));
    for (float value : channel.times) {
      putFloat(result, value);
    }
    for (float value : channel.values) {
      putFloat(result, value);
    }
  }
  if (result.size() > kMaxAssetBytes) {
    error = "Animation exceeds asset size limit";
    return false;
  }
  bytes = std::move(result);
  return true;
}
bool decodeAnimationClip(std::string_view bytes, AnimationClipAsset &asset,
                         std::string &error) {
  error = "Invalid or truncated SANM animation clip";
  if (bytes.size() > kMaxAssetBytes || !bytes.starts_with("SANM")) {
    return false;
  }
  Reader reader(bytes.substr(4));
  AnimationClipAsset candidate;
  std::uint32_t version = 0, loop = 0, count = 0;
  if (!reader.integer(version) || version != 1 ||
      !reader.string(candidate.clip.name) ||
      !reader.number(candidate.clip.duration) ||
      !reader.number(candidate.startSeconds) ||
      !reader.number(candidate.endSeconds) || !reader.integer(loop, 1) ||
      loop > 1 || !reader.number(candidate.speed) || !reader.integer(count) ||
      count > kMaxAnimationJoints) {
    return false;
  }
  candidate.loop = loop != 0;
  candidate.jointNames.resize(count);
  for (auto &name : candidate.jointNames) {
    if (!reader.string(name)) {
      return false;
    }
  }
  if (!reader.integer(count) || count > kMaxAnimationJoints * 3) {
    return false;
  }
  candidate.clip.channels.resize(count);
  for (auto &channel : candidate.clip.channels) {
    std::uint32_t joint = 0, path = 0, interpolation = 0, keys = 0;
    if (!reader.integer(joint, 2) || !reader.integer(path, 1) || path > 2 ||
        !reader.integer(interpolation, 1) || interpolation > 2 ||
        !reader.integer(keys) || keys > kMaxAnimationKeys) {
      return false;
    }
    channel.joint = static_cast<std::uint16_t>(joint);
    channel.path = static_cast<AnimationPath>(path);
    channel.interpolation = static_cast<AnimationInterpolation>(interpolation);
    const std::size_t values =
        std::size_t(keys) * (path == 1 ? 4 : 3) * (interpolation == 2 ? 3 : 1);
    if ((keys + values) > reader.remaining() / 4) {
      return false;
    }
    channel.times.resize(keys);
    channel.values.resize(values);
    for (float &value : channel.times) {
      if (!reader.number(value)) {
        return false;
      }
    }
    for (float &value : channel.values) {
      if (!reader.number(value)) {
        return false;
      }
    }
  }
  if (reader.remaining() || !validateAnimationClip(candidate, error)) {
    return false;
  }
  asset = std::move(candidate);
  error.clear();
  return true;
}
std::vector<int> bindAnimationClip(const AnimationClipAsset &asset,
                                   const AnimationData &mesh) {
  std::vector<int> result(asset.jointNames.size(), -1);
  for (std::size_t i = 0; i < result.size(); ++i) {
    for (std::size_t j = 0; j < mesh.joints.size(); ++j) {
      if (asset.jointNames[i] == mesh.joints[j].name) {
        result[i] = static_cast<int>(j);
        break;
      }
    }
  }
  return result;
}
} // namespace sat
