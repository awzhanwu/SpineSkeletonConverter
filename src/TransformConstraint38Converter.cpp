#include "SkeletonData43.h"

#include <array>
#include <cmath>
#include <functional>
#include <set>

namespace {

using Property = TransformProperty;

// A reset is needed before the next constraint that reads a re-sorted bone, or
// after all constraints if it is re-sorted by the final pass through the bones.
using Restore = std::pair<size_t, size_t>; // constraint order, bone index
using BoneIndices = std::map<std::string, size_t>;
using BoneParents = std::vector<std::optional<size_t>>;

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::vector<bool> activeBones(const SkeletonData& skeleton, const BoneIndices& indices,
                              const BoneParents& parents, const Skin* skin) {
    std::vector<bool> active;
    active.reserve(skeleton.bones.size());
    for (const auto& bone : skeleton.bones) active.push_back(!bone.skinRequired);
    if (skin)
        for (const auto& name : skin->bones)
            for (std::optional<size_t> bone = indices.at(name); bone; bone = parents[*bone])
                active[*bone] = true;
    return active;
}

struct CacheConstraint {
    size_t order = 0;
    OptStr name;
    const std::vector<std::string> Skin::* skinConstraints = nullptr;
    bool skinRequired = false;
    size_t source = 0;
    std::vector<size_t> reads;
    std::vector<size_t> writes;
    std::vector<size_t> invalidates;
    std::vector<size_t> sortedAfter;
    bool localTransform = false;
};

std::vector<size_t> boneIndices(const std::vector<std::string>& names, const BoneIndices& indices) {
    std::vector<size_t> result;
    for (const auto& name : names) result.push_back(indices.at(name));
    return result;
}

void addPathReads(CacheConstraint& constraint, const SkeletonData& skeleton,
                  const std::string& slotName, const BoneIndices& indices) {
    // 3.8 sorts the path dependencies for every skin, including weighted paths.
    for (const auto& skin : skeleton.skins) {
        const auto slot = skin.attachments.find(slotName);
        if (slot == skin.attachments.end()) continue;
        for (const auto& [name, attachment] : slot->second) {
            const auto* path = std::get_if<PathAttachment>(&attachment.data);
            if (!path) continue;
            if (path->vertices.size() == static_cast<size_t>(path->vertexCount) * 2) {
                constraint.reads.push_back(constraint.source);
                continue;
            }
            for (size_t i = 0; i < path->vertices.size();) {
                const auto count = static_cast<size_t>(path->vertices.at(i++));
                for (size_t j = 0; j < count; ++j, i += 4)
                    constraint.reads.push_back(static_cast<size_t>(path->vertices.at(i)));
            }
        }
    }
    constraint.reads.insert(constraint.reads.end(), constraint.writes.begin(), constraint.writes.end());
    for (size_t bone : constraint.reads)
        if (bone >= indices.size()) throw std::runtime_error("Invalid Spine 3.8 path bone index");
}

std::set<Restore> legacyLocalRestores(const Skin* skin, const std::vector<bool>& active,
                                     const std::vector<CacheConstraint>& constraints,
                                     const BoneParents& parents,
                                     const std::vector<std::vector<size_t>>& children) {
    const auto boneCount = parents.size();
    std::vector<bool> sorted(boneCount), hasLocalResult(boneCount);
    for (size_t i = 0; i < boneCount; ++i) sorted[i] = !active[i];

    std::set<Restore> restores;
    size_t order = 0;
    std::function<void(size_t)> sortBone = [&](size_t bone) {
        if (sorted[bone]) return;
        if (parents[bone]) sortBone(*parents[bone]);
        sorted[bone] = true;
        if (hasLocalResult[bone]) {
            restores.emplace(order, bone);
            hasLocalResult[bone] = false;
        }
    };
    std::function<void(size_t)> invalidateChildren = [&](size_t bone) {
        for (size_t child : children[bone]) {
            if (!active[child]) continue;
            if (sorted[child]) invalidateChildren(child);
            sorted[child] = false;
        }
    };
    auto skinHas = [&](const CacheConstraint& constraint) {
        return skin && constraint.name && contains(skin->*constraint.skinConstraints, *constraint.name);
    };
    for (const auto& constraint : constraints) {
        order = constraint.order;
        if (!active[constraint.source] || (constraint.skinRequired && !skinHas(constraint))) continue;
        for (size_t bone : constraint.reads) sortBone(bone);
        // Track results from local transform constraints, including ones a
        // later constraint consumes or modifies before the legacy reset.
        if (constraint.localTransform)
            for (size_t bone : constraint.writes) hasLocalResult[bone] = true;
        for (size_t bone : constraint.invalidates) invalidateChildren(bone);
        for (size_t bone : constraint.sortedAfter) sorted[bone] = true;
    }
    order = constraints.size();
    for (size_t i = 0; i < boneCount; ++i) sortBone(i);
    return restores;
}

struct RestoreActivation {
    bool withoutSkin = false;
    std::vector<size_t> skins;
};

std::map<Restore, RestoreActivation> findRestores(const SkeletonData& skeleton, const BoneIndices& indices,
                                                 const BoneParents& parents,
                                                 const std::vector<std::vector<bool>>& activeBySkin) {
    std::vector<std::vector<size_t>> children(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (parents[i]) children[*parents[i]].push_back(i);
    }

    const auto count = skeleton.ikConstraints.size() + skeleton.transformConstraints.size() + skeleton.pathConstraints.size();
    std::vector<CacheConstraint> constraints;
    constraints.reserve(count);
    for (const auto& ik : skeleton.ikConstraints) {
        const auto source = indices.at(ik.target.value());
        auto bones = boneIndices(ik.bones, indices);
        if (bones.empty()) return {};
        constraints.push_back({ik.order, ik.name, &Skin::ik, ik.skinRequired, source, {source, bones.front()},
                               bones, {bones.front()}, {bones.back()}});
    }
    for (const auto& transform : skeleton.transformConstraints) {
        const auto source = indices.at(transform.target.value());
        auto bones = boneIndices(transform.bones, indices);
        CacheConstraint constraint{transform.order, transform.name, &Skin::transform, transform.skinRequired, source,
                                   {source}, bones, bones, bones, transform.local};
        for (size_t bone : bones) {
            if (!transform.local) constraint.reads.push_back(bone);
            else if (parents[bone]) constraint.reads.push_back(*parents[bone]);
        }
        constraints.push_back(std::move(constraint));
    }
    for (const auto& path : skeleton.pathConstraints) {
        const auto slot = std::find_if(skeleton.slots.begin(), skeleton.slots.end(),
            [&](const SlotData& data) { return data.name == path.target; });
        if (slot == skeleton.slots.end()) return {};
        const auto source = indices.at(slot->bone.value());
        auto bones = boneIndices(path.bones, indices);
        CacheConstraint constraint{path.order, path.name, &Skin::path, path.skinRequired, source, {}, bones, bones, bones};
        addPathReads(constraint, skeleton, path.target.value(), indices);
        constraints.push_back(std::move(constraint));
    }
    std::stable_sort(constraints.begin(), constraints.end(), [](const auto& a, const auto& b) {
        return a.order < b.order;
    });
    std::map<Restore, RestoreActivation> result;
    const auto withoutSkin = activeBones(skeleton, indices, parents, nullptr);
    for (const auto& restore : legacyLocalRestores(nullptr, withoutSkin, constraints, parents, children))
        result[restore].withoutSkin = true;
    for (size_t i = 0; i < skeleton.skins.size(); ++i) {
        const auto& skin = skeleton.skins[i];
        for (const auto& restore : legacyLocalRestores(&skin, activeBySkin[i], constraints, parents, children))
            result[restore].skins.push_back(i);
    }
    return result;
}

std::string uniqueName(std::set<std::string>& names, const std::string& base) {
    if (names.insert(base).second) return base;
    for (size_t i = 2;; ++i) {
        std::string name = base + "-" + std::to_string(i);
        if (names.insert(name).second) return name;
    }
}

BoneData helperBone(std::string name, const OptStr& root) {
    BoneData bone;
    bone.name = std::move(name);
    bone.parent = root;
    bone.visible = false;
    return bone;
}

TransformConstraintData rotationStep(const std::string& name, const std::string& source, const std::string& bone,
                                     bool localSource, bool additive, float from, float to, float scale) {
    TransformConstraintData step;
    step.name = name;
    step.target = source;
    step.bones = {bone};
    step.mixX = step.mixY = step.mixScaleX = step.mixScaleY = step.mixShearY = 0.0f;
    step.mapping = TransformMapping{localSource, true, additive, false, {
        {Property::Rotate, from, {{Property::Rotate, to, 1.0f, scale}}}
    }};
    return step;
}

constexpr std::array mixFields{&TimelineFrame::value1, &TimelineFrame::value2, &TimelineFrame::value3,
                               &TimelineFrame::value4, &TimelineFrame::value5, &TimelineFrame::value6};

void clearMix(TimelineFrame& key, Property property) {
    const auto index = static_cast<size_t>(property);
    key.*mixFields[index] = 0;
    if (key.curveType == CURVE_BEZIER && key.curve.size() >= (index + 1) * 4) {
        key.curve[index * 4 + 1] = 0;
        key.curve[index * 4 + 3] = 0;
    }
}

bool mayMix(const SkeletonData& skeleton, const std::string& name, Property property, float setup) {
    if (setup != 0) return true;
    const auto index = static_cast<size_t>(property);
    for (const auto& animation : skeleton.animations) {
        const auto keys = animation.transform.find(name);
        if (keys == animation.transform.end()) continue;
        for (const auto& key : keys->second)
            if (key.*mixFields[index] != 0 || (key.curveType == CURVE_BEZIER && key.curve.size() >= (index + 1) * 4 &&
                (key.curve[index * 4 + 1] != 0 || key.curve[index * 4 + 3] != 0))) return true;
    }
    return false;
}

bool hasBoneTimeline(const SkeletonData& skeleton, const std::string& bone, const std::string& property) {
    for (const auto& animation : skeleton.animations) {
        const auto keys = animation.bones.find(bone);
        if (keys != animation.bones.end() && keys->second.contains(property)) return true;
    }
    return false;
}

void reportAbsoluteLocalLimitations(const SkeletonData& skeleton, const BoneIndices& indices,
                                    const TransformConstraintData& constraint) {
    const auto& name = constraint.name.value();
    const auto& source = skeleton.bones[indices.at(constraint.target.value())];
    const bool scale = mayMix(skeleton, name, Property::ScaleX, constraint.mixScaleX);
    const bool shear = mayMix(skeleton, name, Property::ShearY, constraint.mixShearY);
    for (const auto& targetName : constraint.bones) {
        const auto& target = skeleton.bones[indices.at(targetName)];
        if (scale && (target.scaleX != 1 || target.scaleY != 1 || hasBoneTimeline(skeleton, targetName, "scale")))
            std::cerr << "Warning: Spine 3.8 absolute local scale on " << targetName << " in " << name
                      << " uses the Spine 4.3 scale mix for non-unit target scales.\n";
        if (shear && (std::abs(std::remainder(source.shearY + constraint.offsetShearY - target.shearY, 360.0f)) > 0.00001f ||
            hasBoneTimeline(skeleton, *source.name, "shear") || hasBoneTimeline(skeleton, targetName, "shear")))
            std::cerr << "Warning: Spine 3.8 absolute local shear on " << targetName << " in " << name
                      << " can accumulate between updates. The converted constraint omits that accumulation.\n";
    }
}

} // namespace

void migrateTransformConstraints38To43(SkeletonData& skeleton) {
    if (std::none_of(skeleton.transformConstraints.begin(), skeleton.transformConstraints.end(),
        [](const auto& constraint) { return constraint.local; })) return;

    const auto original = spine43::orderedConstraints(skeleton);
    for (size_t i = 0; i < original.size(); ++i) {
        if (std::visit([](const auto* data) { return data->order; }, original[i]) != i) {
            std::cerr << "Warning: Spine 3.8 constraint orders must be unique and contiguous to migrate local transforms.\n";
            return;
        }
    }
    BoneIndices indices;
    std::set<std::string> boneNames, constraintNames;
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto& name = skeleton.bones[i].name.value();
        indices.emplace(name, i);
        boneNames.insert(name);
    }
    for (const auto& constraint : original)
        std::visit([&](const auto* data) { constraintNames.insert(data->name.value()); }, constraint);

    BoneParents parents;
    for (const auto& bone : skeleton.bones)
        parents.push_back(bone.parent ? std::optional{indices.at(*bone.parent)} : std::nullopt);
    std::vector<std::vector<bool>> activeBySkin;
    for (const auto& skin : skeleton.skins) activeBySkin.push_back(activeBones(skeleton, indices, parents, &skin));
    const auto restores = findRestores(skeleton, indices, parents, activeBySkin);
    std::vector<std::vector<TransformConstraintData>> before(original.size() + 1);
    std::map<size_t, std::string> poseBones;
    const auto root = skeleton.bones.front().name;
    for (const auto& [where, activation] : restores) {
        const auto [order, boneIndex] = where;
        const bool always = activation.withoutSkin && activation.skins.size() == skeleton.skins.size();
        const auto bone = skeleton.bones.at(boneIndex);
        if (activation.withoutSkin && !always) {
            std::cerr << "Warning: Spine 3.8 local pose reset for " << *bone.name
                      << " depends on the absence of a skin and cannot be represented for all skins.\n";
        }
        // 4.3 can gate a constraint on an active skin, including multiple skins
        // listed in the export. It cannot test that no skin is selected.
        std::vector<size_t> supportedSkins;
        for (size_t index : activation.skins)
            if (skeleton.skins[index].name != "default") supportedSkins.push_back(index);
        if (!always && supportedSkins.size() != activation.skins.size())
            std::cerr << "Warning: A skin-specific Spine 3.8 local pose reset on the default skin is not encoded in SKEL.\n";
        if (!always && supportedSkins.empty()) continue;

        auto [pose, added] = poseBones.try_emplace(boneIndex);
        if (added) {
            pose->second = uniqueName(boneNames, "__spine38_pose_" + *bone.name);
            auto copy = helperBone(pose->second, root);
            copy.x = bone.x;
            copy.y = bone.y;
            copy.rotation = bone.rotation;
            copy.scaleX = bone.scaleX;
            copy.scaleY = bone.scaleY;
            copy.shearY = bone.shearY;
            skeleton.bones.push_back(std::move(copy));
            for (auto& animation : skeleton.animations) {
                const auto keys = animation.bones.find(*bone.name);
                if (keys == animation.bones.end()) continue;
                MultiTimeline localPose;
                for (const auto& [type, timeline] : keys->second)
                    if (type != "inherit") localPose.emplace(type, timeline);
                if (!localPose.empty()) animation.bones.emplace(pose->second, std::move(localPose));
            }
        }

        TransformConstraintData restore;
        restore.name = uniqueName(constraintNames, "__spine38_restore_" + *bone.name);
        restore.bones = {*bone.name};
        restore.target = pose->second;
        restore.local = true;
        restore.skinRequired = !always;
        for (size_t index : supportedSkins)
            if (restore.skinRequired) skeleton.skins[index].transform.push_back(*restore.name);
        before[order].push_back(std::move(restore));
    }

    OptStr angleBone;
    for (auto& transform : skeleton.transformConstraints) {
        if (!transform.local || transform.relative) continue;
        reportAbsoluteLocalLimitations(skeleton, indices, transform);
        const auto& name = transform.name.value();
        const auto sourceIndex = indices.at(transform.target.value());
        auto mapping = spine43::transformMapping(transform);
        // The 3.8 C++ runtime writes absolute local shear to the unconstrained
        // pose. The immediate world transform uses the previous applied shear.
        std::erase_if(mapping.properties, [](const auto& from) {
            return from.property == Property::Rotate || from.property == Property::ShearY;
        });
        if (mayMix(skeleton, name, Property::Rotate, transform.mixRotate)) {
            if (!angleBone) {
                auto angle = helperBone(uniqueName(boneNames, "__spine38_angle"), root);
                angle.inherit = Inherit_OnlyTranslation;
                skeleton.bones.push_back(angle);
                angleBone = std::move(angle.name);
            }
            const bool skinRequired = transform.skinRequired || skeleton.bones[sourceIndex].skinRequired;
            std::vector<size_t> enabledSkins;
            if (skinRequired) {
                for (size_t i = 0; i < skeleton.skins.size(); ++i) {
                    const auto& skin = skeleton.skins[i];
                    if (!activeBySkin[i][sourceIndex] || (transform.skinRequired && !contains(skin.transform, name))) continue;
                    enabledSkins.push_back(i);
                }
            }
            // atan2 on a rotation-only bone provides the legacy shortest arc.
            // 4.3 world rotations are exposed in [0, 360), so shift by 180
            // while taking the difference and subtract it again on application.
            for (const auto& bone : transform.bones) {
                const auto prefix = "__spine38_" + name + "_" + bone;
                auto source = rotationStep(uniqueName(constraintNames, prefix + "_source"),
                    *transform.target, *angleBone, true, false, 0, 180 + transform.offsetRotation, 1);
                auto difference = rotationStep(uniqueName(constraintNames, prefix + "_difference"),
                    bone, *angleBone, true, true, 0, 0, -1);
                auto apply = rotationStep(uniqueName(constraintNames, prefix + "_rotate"),
                    *angleBone, bone, false, true, 180, 0, 1);
                apply.mixRotate = transform.mixRotate;
                for (auto& animation : skeleton.animations) {
                    const auto keys = animation.transform.find(name);
                    if (keys == animation.transform.end()) continue;
                    auto rotate = keys->second;
                    for (auto& key : rotate)
                        for (Property property : {Property::X, Property::Y, Property::ScaleX, Property::ScaleY, Property::ShearY})
                            clearMix(key, property);
                    animation.transform.emplace(*apply.name, std::move(rotate));
                }
                // Each step must be inactive whenever the source constraint is
                // inactive, even though the scratch bone itself is always active.
                for (auto* step : {&source, &difference, &apply}) {
                    step->skinRequired = skinRequired;
                    for (size_t i : enabledSkins) skeleton.skins[i].transform.push_back(*step->name);
                    before[transform.order].push_back(std::move(*step));
                }
            }
        }
        transform.mixRotate = transform.mixShearY = 0;
        for (auto& animation : skeleton.animations) {
            const auto keys = animation.transform.find(name);
            if (keys == animation.transform.end()) continue;
            for (auto& key : keys->second) {
                clearMix(key, Property::Rotate);
                clearMix(key, Property::ShearY);
            }
        }
        transform.mapping = std::move(mapping);
    }
    // Visibility is editor metadata and must survive the binary write too.
    if (angleBone || !poseBones.empty()) skeleton.nonessential = true;

    // Insert the generated operations in the single 4.3 constraint sequence
    // consumed by both writers. Preserve the relative order of authored constraints.
    std::vector<TransformConstraintData> generated;
    std::vector<size_t> originalOrder(original.size());
    size_t nextOrder = 0;
    for (size_t order = 0; order < before.size(); ++order) {
        for (auto& step : before[order]) {
            step.order = nextOrder++;
            generated.push_back(std::move(step));
        }
        if (order < original.size()) originalOrder[order] = nextOrder++;
    }
    for (auto& constraint : skeleton.ikConstraints) constraint.order = originalOrder[constraint.order];
    for (auto& constraint : skeleton.transformConstraints) constraint.order = originalOrder[constraint.order];
    for (auto& constraint : skeleton.pathConstraints) constraint.order = originalOrder[constraint.order];
    for (auto& constraint : generated) skeleton.transformConstraints.push_back(std::move(constraint));
}
