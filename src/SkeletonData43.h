#pragma once

#include "SkeletonData.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace spine42 {
SkeletonData readLegacy43BinaryData(const Binary&);
}

namespace spine43 {

enum class ConstraintType { Ik, Path, Transform, Physics, Slider };
using Constraint = std::variant<const IKConstraintData*, const PathConstraintData*,
    const TransformConstraintData*, const PhysicsConstraintData*, const SliderData*>;

inline constexpr std::array constraintNames{"ik", "path", "transform", "physics", "slider"};
inline constexpr std::array propertyNames{"rotate", "x", "y", "scaleX", "scaleY", "shearY"};
inline constexpr std::array scaleYNames{"none", "uniform", "volume"};
inline constexpr Color boneColor{156, 156, 156, 255};
inline constexpr Color skinColor{254, 158, 79, 255};
inline constexpr Color boxColor{97, 240, 0, 255};
inline constexpr Color pointColor{241, 241, 0, 255};
inline constexpr Color transparent{0, 0, 0, 0};

template <class Enum, size_t N>
Enum enumValue(const std::array<const char*, N>& names, std::string_view name) {
    auto it = std::find(names.begin(), names.end(), name);
    if (it == names.end()) throw std::runtime_error("Unknown Spine 4.3 value: " + std::string(name));
    return static_cast<Enum>(it - names.begin());
}

inline std::vector<Constraint> orderedConstraints(const SkeletonData& skeleton) {
    std::vector<Constraint> constraints;
    for (const auto& data : skeleton.ikConstraints) constraints.emplace_back(&data);
    for (const auto& data : skeleton.pathConstraints) constraints.emplace_back(&data);
    for (const auto& data : skeleton.transformConstraints) constraints.emplace_back(&data);
    for (const auto& data : skeleton.physicsConstraints) constraints.emplace_back(&data);
    for (const auto& data : skeleton.sliders) constraints.emplace_back(&data);
    auto order = [](const Constraint& constraint) {
        return std::visit([](auto* data) { return data->order; }, constraint);
    };
    std::stable_sort(constraints.begin(), constraints.end(),
        [&](const Constraint& a, const Constraint& b) { return order(a) < order(b); });
    return constraints;
}

inline ScaleYMode scaleYMode(const IKConstraintData& ik) {
    return ik.uniform ? ScaleYMode::Uniform : ik.scaleY;
}

inline TransformMapping transformMapping(const TransformConstraintData& transform) {
    if (transform.mapping) return *transform.mapping;

    // Older constraints copy each source component to the corresponding target component.
    TransformMapping mapping{transform.local, transform.local, transform.relative, false, {}};
    for (size_t i = 0; i < propertyNames.size(); ++i) {
        auto property = static_cast<TransformProperty>(i);
        mapping.properties.push_back({property, 0.0f, {{property}}});
    }
    return mapping;
}

inline std::optional<int> binaryMeshHull(const MeshAttachment& mesh) {
    const auto coordinates = mesh.uvs.size();
    const auto triangles = mesh.triangles.size() / 3;
    if (coordinates % 2 || mesh.triangles.size() % 3 || coordinates < triangles + 2) return std::nullopt;
    const auto hull = coordinates - triangles - 2;
    if (hull > coordinates / 2) return std::nullopt;
    return static_cast<int>(hull);
}

} // namespace spine43
