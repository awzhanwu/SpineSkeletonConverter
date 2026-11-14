#include "SkeletonData43.h"

#include <bit>
#include <cmath>
#include <set>
#include <tuple>

namespace spine43 {
namespace {

using Indices = std::map<std::string, int, std::less<>>;
using MeshKey = std::tuple<std::string, std::string, std::string>;

int indexOf(const Indices& indices, std::string_view name, const char* kind) {
    auto it = indices.find(name);
    if (it == indices.end()) throw std::runtime_error(std::string("Unknown ") + kind + ": " + std::string(name));
    return it->second;
}

int indexOf(const Indices& indices, const std::string& name, const char* kind) {
    return indexOf(indices, std::string_view(name), kind);
}

int indexOf(const Indices& indices, const OptStr& name, const char* kind) {
    if (!name) throw std::runtime_error(std::string("Missing ") + kind + " name");
    return indexOf(indices, *name, kind);
}

template <class T>
Indices indicesOf(const std::vector<T>& items) {
    Indices indices;
    for (size_t i = 0; i < items.size(); ++i) {
        const OptStr& name = items[i].name;
        if (!name || name->empty() || !indices.emplace(*name, static_cast<int>(i)).second)
            throw std::runtime_error("Missing or duplicate name: " + name.value_or(""));
    }
    return indices;
}

struct WriteContext {
    const SkeletonData& data;
    const Indices bones, slots, animations, events;
    const std::vector<Constraint> constraints;
    std::array<Indices, constraintNames.size()> constraintIndices;
    std::vector<const Skin*> skins;
    Indices skinIndices;
    Indices strings;
    std::map<MeshKey, std::vector<int>> timelineSlots;

    explicit WriteContext(const SkeletonData& skeleton);

    int constraintIndex(ConstraintType type, const std::string& name) const {
        auto i = static_cast<size_t>(type);
        return indexOf(constraintIndices[i], name, constraintNames[i]);
    }

    void stringRef(Binary& binary, const OptStr& value) const {
        writeVarint(binary, value ? indexOf(strings, *value, "string reference") : 0, true);
    }

    const Attachment& attachment(const MeshKey& key) const {
        const auto& [skinName, slotName, name] = key;
        const auto& skin = *skins[indexOf(skinIndices, skinName, "skin")];
        auto slot = skin.attachments.find(slotName);
        if (slot != skin.attachments.end()) {
            auto it = slot->second.find(name);
            if (it != slot->second.end()) return it->second;
        }
        throw std::runtime_error("Unknown attachment: " + skinName + "/" + slotName + "/" + name);
    }
};

MeshKey sourceMeshKey(const LinkedmeshAttachment& mesh, const std::string& slot) {
    const auto skin = mesh.skin && !mesh.skin->empty() ? *mesh.skin : "default";
    return {skin, mesh.sourceSlot.value_or(slot), mesh.parentMesh};
}

WriteContext::WriteContext(const SkeletonData& skeleton)
    : data(skeleton), bones(indicesOf(data.bones)), slots(indicesOf(data.slots)),
      animations(indicesOf(data.animations)), events(indicesOf(data.events)),
      constraints(orderedConstraints(data)) {
    for (size_t i = 0; i < constraints.size(); ++i) {
        std::visit([&](auto* constraint) {
            if (!constraint->name || !constraintIndices[constraints[i].index()].emplace(*constraint->name, static_cast<int>(i)).second)
                throw std::runtime_error("Missing or duplicate constraint name");
        }, constraints[i]);
    }

    const Skin* defaultSkin = nullptr;
    for (const auto& skin : data.skins) {
        if (skin.name == "default") {
            if (defaultSkin) throw std::runtime_error("Duplicate default skin");
            defaultSkin = &skin;
        } else {
            skins.push_back(&skin);
        }
    }
    // A zero slot count omits the default skin from the runtime skin array.
    if (defaultSkin && !defaultSkin->attachments.empty()) skins.insert(skins.begin(), defaultSkin);
    for (size_t i = 0; i < skins.size(); ++i) {
        if (!skinIndices.emplace(skins[i]->name, static_cast<int>(i)).second)
            throw std::runtime_error("Duplicate skin: " + skins[i]->name);
    }

    for (const auto& slot : data.slots) {
        if (slot.attachmentName) strings.emplace(*slot.attachmentName, 0);
    }
    for (const auto* skin : skins) {
        for (const auto& [slot, attachments] : skin->attachments) {
            for (const auto& [placeholder, attachment] : attachments) {
                strings.emplace(placeholder, 0);
                if (attachment.name != placeholder) strings.emplace(attachment.name, 0);
                if (attachment.path != attachment.name) strings.emplace(attachment.path, 0);
                if (const auto* linked = std::get_if<LinkedmeshAttachment>(&attachment.data)) {
                    const auto key = sourceMeshKey(*linked, slot);
                    if (this->attachment(key).type != AttachmentType_Mesh)
                        throw std::runtime_error("Linked mesh source must be a mesh: " + linked->parentMesh);
                    if (linked->timelines && std::get<1>(key) != slot) {
                        auto& targets = timelineSlots[key];
                        int index = indexOf(slots, slot, "slot");
                        if (std::find(targets.begin(), targets.end(), index) == targets.end()) targets.push_back(index);
                    }
                }
            }
        }
    }
    for (const auto& animation : data.animations) {
        for (const auto& [slot, timelines] : animation.slots) {
            auto swaps = timelines.find("attachment");
            if (swaps == timelines.end()) continue;
            for (const auto& frame : swaps->second) {
                if (frame.str1) strings.emplace(*frame.str1, 0);
            }
        }
    }
    int index = 1;
    for (auto& [string, value] : strings) value = index++;
}

void writeSequence(Binary& binary, const Sequence& sequence) {
    if (sequence.count <= 0 || sequence.start < 0 || sequence.digits < 0 ||
        sequence.setupIndex < 0 || sequence.setupIndex >= sequence.count)
        throw std::runtime_error("Invalid attachment sequence");
    writeVarint(binary, sequence.count, true);
    writeVarint(binary, sequence.start, true);
    writeVarint(binary, sequence.digits, true);
    writeVarint(binary, sequence.setupIndex, true);
}

void writeVertices(Binary& binary, const std::vector<float>& vertices, size_t vertexCount, const WriteContext& context) {
    writeVarint(binary, vertexCount, true);
    if (vertices.size() == vertexCount * 2) {
        for (float value : vertices) writeFloat(binary, value);
        return;
    }
    Binary weights;
    size_t position = 0, boneEntries = 0;
    for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
        if (position >= vertices.size()) throw std::runtime_error("Missing weighted vertices");
        const float count = vertices[position++];
        if (count < 1 || count > (vertices.size() - position) / 4 || std::floor(count) != count)
            throw std::runtime_error("Invalid weighted vertex bone count");
        const int bones = static_cast<int>(count);
        writeVarint(weights, bones, true);
        boneEntries += bones + 1;
        for (int i = 0; i < bones; ++i) {
            const float bone = vertices[position++];
            if (bone < 0 || bone >= context.data.bones.size() || std::floor(bone) != bone)
                throw std::runtime_error("Invalid weighted vertex bone index");
            writeVarint(weights, static_cast<int>(bone), true);
            for (int component = 0; component < 3; ++component) writeFloat(weights, vertices[position++]);
        }
    }
    if (position != vertices.size()) throw std::runtime_error("Unexpected data after weighted vertices");
    writeVarint(binary, boneEntries, true);
    binary.insert(binary.end(), weights.begin(), weights.end());
}

void writeAttachment(Binary& binary, const std::string& skin, const std::string& slot,
                     const std::string& placeholder, const Attachment& attachment, const WriteContext& context) {
    const bool nonessential = context.data.nonessential;
    const bool customPath = attachment.path != attachment.name;
    unsigned char flags = static_cast<unsigned char>(attachment.type);
    if (attachment.name != placeholder) flags |= 8;
    Binary payload;
    auto textured = [&](const auto& data) {
        if (customPath) {
            flags |= 16;
            context.stringRef(payload, attachment.path);
        }
        if (data.color) {
            flags |= 32;
            writeColor(payload, *data.color);
        }
        if (data.sequence) {
            flags |= 64;
            writeSequence(payload, *data.sequence);
        }
    };
    switch (attachment.type) {
        case AttachmentType_Region: {
            const auto& region = std::get<RegionAttachment>(attachment.data);
            textured(region);
            if (region.rotation != 0.0f) {
                flags |= 128;
                writeFloat(payload, region.rotation);
            }
            for (float value : {region.x, region.y, region.scaleX, region.scaleY, region.width, region.height})
                writeFloat(payload, value);
            break;
        }
        case AttachmentType_Boundingbox: {
            const auto& box = std::get<BoundingboxAttachment>(attachment.data);
            if (box.vertices.size() != 2 * box.vertexCount) flags |= 16;
            writeVertices(payload, box.vertices, box.vertexCount, context);
            if (nonessential) writeColor(payload, box.color.value_or(boxColor));
            break;
        }
        case AttachmentType_Mesh: {
            const auto& mesh = std::get<MeshAttachment>(attachment.data);
            textured(mesh);
            if (mesh.vertices.size() != mesh.uvs.size()) flags |= 128;
            // The runtime infers the triangle index count from this field.
            const auto hull = binaryMeshHull(mesh);
            if (!hull) throw std::runtime_error("Invalid mesh topology: " + attachment.name);
            writeVarint(payload, *hull, true);
            writeVertices(payload, mesh.vertices, mesh.uvs.size() / 2, context);
            for (float uv : mesh.uvs) writeFloat(payload, uv);
            for (auto index : mesh.triangles) {
                if (index >= mesh.uvs.size() / 2) throw std::runtime_error("Invalid mesh triangle index: " + attachment.name);
                writeVarint(payload, index, true);
            }
            auto targets = context.timelineSlots.find({skin, slot, placeholder});
            writeVarint(payload, targets == context.timelineSlots.end() ? 0 : targets->second.size(), true);
            if (targets != context.timelineSlots.end())
                for (int index : targets->second) writeVarint(payload, index, true);
            if (nonessential) {
                writeVarint(payload, mesh.edges.size(), true);
                for (auto index : mesh.edges) writeVarint(payload, index, true);
                writeFloat(payload, mesh.width);
                writeFloat(payload, mesh.height);
            }
            break;
        }
        case AttachmentType_Linkedmesh: {
            const auto& mesh = std::get<LinkedmeshAttachment>(attachment.data);
            textured(mesh);
            if (mesh.timelines) flags |= 128;
            const auto [sourceSkin, sourceSlot, source] = sourceMeshKey(mesh, slot);
            writeVarint(payload, indexOf(context.slots, sourceSlot, "source slot"), true);
            writeVarint(payload, indexOf(context.skinIndices, sourceSkin, "source skin"), true);
            context.stringRef(payload, source);
            if (nonessential) {
                writeFloat(payload, mesh.width);
                writeFloat(payload, mesh.height);
            }
            break;
        }
        case AttachmentType_Path: {
            const auto& path = std::get<PathAttachment>(attachment.data);
            if (path.closed) flags |= 16;
            if (path.constantSpeed) flags |= 32;
            if (path.vertices.size() != 2 * path.vertexCount) flags |= 64;
            if (path.lengths.size() != path.vertexCount / 3)
                throw std::runtime_error("Invalid path length count: " + attachment.name);
            writeVertices(payload, path.vertices, path.vertexCount, context);
            for (float length : path.lengths) writeFloat(payload, length);
            if (nonessential) writeColor(payload, path.color.value_or(transparent));
            break;
        }
        case AttachmentType_Point: {
            const auto& point = std::get<PointAttachment>(attachment.data);
            for (float value : {point.rotation, point.x, point.y}) writeFloat(payload, value);
            if (nonessential) writeColor(payload, point.color.value_or(pointColor));
            break;
        }
        case AttachmentType_Clipping: {
            const auto& clip = std::get<ClippingAttachment>(attachment.data);
            if (clip.vertices.size() != 2 * clip.vertexCount) flags |= 16;
            if (clip.convex) flags |= 32;
            if (clip.inverse) flags |= 64;
            // Clipping without an end slot lasts until the end of rendering.
            // The binary reader requires a real slot index, so use the start slot.
            writeVarint(payload, indexOf(context.slots, clip.endSlot.value_or(slot), "clipping end slot"), true);
            writeVertices(payload, clip.vertices, clip.vertexCount, context);
            if (nonessential) writeColor(payload, clip.color.value_or(transparent));
            break;
        }
    }
    writeByte(binary, flags);
    if (flags & 8) context.stringRef(binary, attachment.name);
    binary.insert(binary.end(), payload.begin(), payload.end());
}

void writeSkin(Binary& binary, const Skin& skin, const WriteContext& context, bool isDefault) {
    if (!isDefault) {
        writeString(binary, skin.name);
        if (context.data.nonessential) writeColor(binary, skin.color.value_or(skinColor));
        writeVarint(binary, skin.bones.size(), true);
        for (const auto& bone : skin.bones) writeVarint(binary, indexOf(context.bones, bone, "skin bone"), true);

        writeVarint(binary, skin.ik.size() + skin.transform.size() + skin.path.size() + skin.physics.size() + skin.slider.size(), true);
        for (const auto& name : skin.ik) writeVarint(binary, context.constraintIndex(ConstraintType::Ik, name), true);
        for (const auto& name : skin.transform) writeVarint(binary, context.constraintIndex(ConstraintType::Transform, name), true);
        for (const auto& name : skin.path) writeVarint(binary, context.constraintIndex(ConstraintType::Path, name), true);
        for (const auto& name : skin.physics) writeVarint(binary, context.constraintIndex(ConstraintType::Physics, name), true);
        for (const auto& name : skin.slider) writeVarint(binary, context.constraintIndex(ConstraintType::Slider, name), true);
    }
    writeVarint(binary, skin.attachments.size(), true);
    for (const auto& [slot, attachments] : skin.attachments) {
        writeVarint(binary, indexOf(context.slots, slot, "skin slot"), true);
        writeVarint(binary, attachments.size(), true);
        for (const auto& [placeholder, attachment] : attachments) {
            context.stringRef(binary, placeholder);
            writeAttachment(binary, skin.name, slot, placeholder, attachment, context);
        }
    }
}

void writeConstraintBones(Binary& binary, const std::vector<std::string>& bones, const WriteContext& context) {
    writeVarint(binary, bones.size(), true);
    for (const auto& bone : bones) writeVarint(binary, indexOf(context.bones, bone, "constraint bone"), true);
}

void writeFlaggedFloats(Binary& binary, std::initializer_list<float> values) {
    unsigned char flags = 0, bit = 1;
    for (float value : values) {
        if (value != 0.0f) flags |= bit;
        bit <<= 1;
    }
    writeByte(binary, flags);
    for (float value : values) {
        if (value != 0.0f) writeFloat(binary, value);
    }
}

void writeConstraint(Binary& binary, const IKConstraintData& ik, const WriteContext& context) {
    writeConstraintBones(binary, ik.bones, context);
    writeVarint(binary, indexOf(context.bones, ik.target, "IK target"), true);
    const auto scaleY = scaleYMode(ik);
    unsigned char flags = ik.skinRequired;
    if (scaleY != ScaleYMode::None) flags |= 2;
    if (!ik.bendPositive) flags |= 4;
    if (ik.compress) flags |= 8;
    if (ik.stretch) flags |= 16;
    if (ik.mix != 0.0f) flags |= 32;
    if (ik.mix != 0.0f && ik.mix != 1.0f) flags |= 64;
    if (ik.softness != 0.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 2) writeByte(binary, static_cast<unsigned char>(scaleY));
    if (flags & 64) writeFloat(binary, ik.mix);
    if (flags & 128) writeFloat(binary, ik.softness);
}

void writeConstraint(Binary& binary, const TransformConstraintData& transform, const WriteContext& context) {
    writeConstraintBones(binary, transform.bones, context);
    writeVarint(binary, indexOf(context.bones, transform.target, "transform source"), true);
    const auto mapping = transformMapping(transform);
    if (mapping.properties.size() > 7) throw std::runtime_error("Too many transform source properties");
    unsigned char flags = transform.skinRequired | (mapping.properties.size() << 5);
    if (mapping.localSource) flags |= 2;
    if (mapping.localTarget) flags |= 4;
    if (mapping.additive) flags |= 8;
    if (mapping.clamp) flags |= 16;
    writeByte(binary, flags);
    for (const auto& from : mapping.properties) {
        if (from.to.size() > 127) throw std::runtime_error("Too many transform destination properties");
        writeByte(binary, static_cast<unsigned char>(from.property));
        writeFloat(binary, from.offset);
        writeByte(binary, static_cast<unsigned char>(from.to.size()));
        for (const auto& to : from.to) {
            writeByte(binary, static_cast<unsigned char>(to.property));
            writeFloat(binary, to.offset);
            writeFloat(binary, to.max);
            writeFloat(binary, to.scale);
        }
    }
    writeFlaggedFloats(binary, {transform.offsetRotation, transform.offsetX, transform.offsetY,
                                transform.offsetScaleX, transform.offsetScaleY, transform.offsetShearY});
    writeFlaggedFloats(binary, {transform.mixRotate, transform.mixX, transform.mixY,
                                transform.mixScaleX, transform.mixScaleY, transform.mixShearY});
}

void writeConstraint(Binary& binary, const PathConstraintData& path, const WriteContext& context) {
    writeConstraintBones(binary, path.bones, context);
    writeVarint(binary, indexOf(context.slots, path.target, "path slot"), true);
    unsigned char flags = path.skinRequired | (path.positionMode << 1) | (path.spacingMode << 2) | (path.rotateMode << 4);
    if (path.offsetRotation != 0.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 128) writeFloat(binary, path.offsetRotation);
    for (float value : {path.position, path.spacing, path.mixRotate, path.mixX, path.mixY}) writeFloat(binary, value);
}

void writeConstraint(Binary& binary, const PhysicsConstraintData& physics, const WriteContext& context) {
    writeVarint(binary, indexOf(context.bones, physics.bone, "physics bone"), true);
    if (physics.fps < 1 || physics.fps > 255 || std::floor(physics.fps) != physics.fps)
        throw std::runtime_error("Physics FPS must be an integer from 1 to 255");
    float scaleX = physics.scaleX;
    if (physics.scaleY == ScaleYMode::Uniform) scaleX = -1.0f - scaleX;
    if (physics.scaleY == ScaleYMode::Volume) scaleX = -2.0f - scaleX;
    if ((physics.scaleY == ScaleYMode::None && scaleX < 0) ||
        (physics.scaleY == ScaleYMode::Uniform && (scaleX >= 0 || scaleX < -2)) ||
        (physics.scaleY == ScaleYMode::Volume && scaleX >= -2))
        throw std::runtime_error("Physics scaleX and scaleY combination cannot be encoded");

    unsigned char flags = physics.skinRequired;
    if (physics.x != 0.0f) flags |= 2;
    if (physics.y != 0.0f) flags |= 4;
    if (physics.rotate != 0.0f) flags |= 8;
    if (scaleX != 0.0f) flags |= 16;
    if (physics.shearX != 0.0f) flags |= 32;
    if (physics.limit != 5000.0f) flags |= 64;
    if (physics.mass != 1.0f) flags |= 128;
    writeByte(binary, flags);
    if (flags & 2) writeFloat(binary, physics.x);
    if (flags & 4) writeFloat(binary, physics.y);
    if (flags & 8) writeFloat(binary, physics.rotate);
    if (flags & 16) writeFloat(binary, scaleX);
    if (flags & 32) writeFloat(binary, physics.shearX);
    if (flags & 64) writeFloat(binary, physics.limit);
    writeByte(binary, static_cast<unsigned char>(physics.fps));
    writeFloat(binary, physics.inertia);
    writeFloat(binary, physics.strength);
    writeFloat(binary, physics.damping);
    if (flags & 128) writeFloat(binary, 1.0f / physics.mass);
    writeFloat(binary, physics.wind);
    writeFloat(binary, physics.gravity);

    flags = physics.inertiaGlobal | (physics.strengthGlobal << 1) | (physics.dampingGlobal << 2) |
        (physics.massGlobal << 3) | (physics.windGlobal << 4) | (physics.gravityGlobal << 5) |
        (physics.mixGlobal << 6) | ((physics.mix != 1.0f) << 7);
    writeByte(binary, flags);
    if (flags & 128) writeFloat(binary, physics.mix);
}

void writeConstraint(Binary& binary, const SliderData& slider, const WriteContext& context) {
    const float timeOrMax = slider.bone ? slider.max : slider.time;
    unsigned char flags = slider.skinRequired | (slider.loop << 1) | (slider.additive << 2);
    if (timeOrMax != 0.0f) flags |= 8;
    if (slider.mix != 0.0f) flags |= 16;
    if (slider.mix != 0.0f && slider.mix != 1.0f) flags |= 32;
    if (slider.bone) flags |= 64;
    if (slider.local) flags |= 128;
    writeByte(binary, flags);
    if (flags & 8) writeFloat(binary, timeOrMax);
    if (flags & 32) writeFloat(binary, slider.mix);
    if (slider.bone) {
        writeVarint(binary, indexOf(context.bones, slider.bone, "slider bone"), true);
        writeFloat(binary, slider.from);
        writeByte(binary, static_cast<unsigned char>(slider.property));
        writeFloat(binary, slider.to);
        writeFloat(binary, slider.scale);
    }
}

int bezierCount(const Timeline& keys, size_t channels) {
    int count = 0;
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (keys[i].curveType == CURVE_BEZIER) {
            if (keys[i].curve.size() != channels * 4)
                throw std::runtime_error("Bezier control point count does not match timeline channels");
            count += channels;
        }
    }
    return count;
}

void writeCurve(Binary& binary, const TimelineFrame& key) {
    writeByte(binary, key.curveType);
    if (key.curveType == CURVE_BEZIER) {
        for (float value : key.curve) writeFloat(binary, value);
    }
}

template <class WriteValues>
void writeCurveTimeline(Binary& binary, const Timeline& keys, size_t channels, WriteValues writeValues) {
    writeVarint(binary, bezierCount(keys, channels), true);
    for (size_t i = 0; i < keys.size(); ++i) {
        writeFloat(binary, keys[i].time);
        writeValues(keys[i]);
        if (i > 0) writeCurve(binary, keys[i - 1]);
    }
}

void writeFloatTimeline(Binary& binary, const Timeline& keys, size_t channels) {
    writeCurveTimeline(binary, keys, channels, [&](const TimelineFrame& key) {
        const std::array values{key.value1, key.value2, key.value3, key.value4, key.value5, key.value6};
        for (size_t i = 0; i < channels; ++i) writeFloat(binary, values[i]);
    });
}

template <class WriteGroup>
void writeGroups(Binary& binary, const std::map<std::string, MultiTimeline>& groups, WriteGroup writeGroup) {
    Binary payload;
    size_t groupCount = 0;
    for (const auto& [name, timelines] : groups) {
        const auto count = std::count_if(timelines.begin(), timelines.end(), [](const auto& entry) { return !entry.second.empty(); });
        if (count == 0) continue;
        writeGroup(payload, name, count, timelines);
        ++groupCount;
    }
    writeVarint(binary, groupCount, true);
    binary.insert(binary.end(), payload.begin(), payload.end());
}

template <class TypeMap, class WriteKeys>
void writeTimelines(Binary& binary, const MultiTimeline& timelines, const TypeMap& types, WriteKeys writeKeys) {
    for (const auto& [name, keys] : timelines) {
        if (keys.empty()) continue;
        auto type = types.at(name);
        writeByte(binary, static_cast<unsigned char>(type));
        writeVarint(binary, keys.size(), true);
        writeKeys(type, keys);
    }
}

void writeSlotTimeline(Binary& binary, SlotTimelineType type, const Timeline& keys, const WriteContext& context) {
    if (type == SLOT_ATTACHMENT) {
        for (const auto& key : keys) {
            writeFloat(binary, key.time);
            context.stringRef(binary, key.str1);
        }
        return;
    }
    constexpr std::array channels{0, 4, 3, 7, 6, 1};
    writeCurveTimeline(binary, keys, channels[type], [&](const TimelineFrame& key) {
        if (type == SLOT_ALPHA) {
            writeByte(binary, static_cast<unsigned char>(std::lround(std::clamp(key.value1, 0.0f, 1.0f) * 255.0f)));
        } else {
            writeColor(binary, key.color1.value(), type == SLOT_RGBA || type == SLOT_RGBA2);
            if (type == SLOT_RGBA2 || type == SLOT_RGB2) writeColor(binary, key.color2.value(), false);
        }
    });
}

void writeIkTimeline(Binary& binary, const Timeline& keys) {
    writeVarint(binary, bezierCount(keys, 2), true);
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        const auto curve = i == 0 ? CURVE_LINEAR : keys[i - 1].curveType;
        unsigned char flags = 0;
        if (key.value1 != 0.0f) flags |= 1;
        if (key.value1 != 0.0f && key.value1 != 1.0f) flags |= 2;
        if (key.value2 != 0.0f) flags |= 4;
        if (key.bendPositive) flags |= 8;
        if (key.compress) flags |= 16;
        if (key.stretch) flags |= 32;
        if (curve == CURVE_STEPPED) flags |= 64;
        if (curve == CURVE_BEZIER) flags |= 128;
        writeByte(binary, flags);
        writeFloat(binary, key.time);
        if (flags & 2) writeFloat(binary, key.value1);
        if (flags & 4) writeFloat(binary, key.value2);
        if (flags & 128) {
            for (float value : keys[i - 1].curve) writeFloat(binary, value);
        }
    }
}

size_t deformLength(const std::vector<float>& vertices, size_t vertexCount) {
    if (vertices.size() == vertexCount * 2) return vertices.size();
    size_t length = 0;
    for (size_t i = 0; i < vertices.size();) {
        const float count = vertices[i++];
        if (count < 1 || count > (vertices.size() - i) / 4 || std::floor(count) != count)
            throw std::runtime_error("Invalid weighted deform attachment");
        i += static_cast<size_t>(count) * 4;
        length += static_cast<size_t>(count) * 2;
    }
    return length;
}

size_t deformLength(const Attachment& attachment, const WriteContext& context, const std::string& slot) {
    switch (attachment.type) {
        case AttachmentType_Mesh: {
            const auto& mesh = std::get<MeshAttachment>(attachment.data);
            return deformLength(mesh.vertices, mesh.uvs.size() / 2);
        }
        case AttachmentType_Boundingbox: {
            const auto& box = std::get<BoundingboxAttachment>(attachment.data);
            return deformLength(box.vertices, box.vertexCount);
        }
        case AttachmentType_Path: {
            const auto& path = std::get<PathAttachment>(attachment.data);
            return deformLength(path.vertices, path.vertexCount);
        }
        case AttachmentType_Clipping: {
            const auto& clip = std::get<ClippingAttachment>(attachment.data);
            return deformLength(clip.vertices, clip.vertexCount);
        }
        case AttachmentType_Linkedmesh:
            return deformLength(context.attachment(sourceMeshKey(std::get<LinkedmeshAttachment>(attachment.data), slot)), context, slot);
        default:
            throw std::runtime_error("Deform timeline requires a vertex attachment: " + attachment.name);
    }
}

void writeAttachmentTimelines(Binary& binary, const Animation& animation, const WriteContext& context) {
    writeVarint(binary, animation.attachments.size(), true);
    for (const auto& [skin, slots] : animation.attachments) {
        writeVarint(binary, indexOf(context.skinIndices, skin, "timeline skin"), true);
        writeVarint(binary, slots.size(), true);
        for (const auto& [slot, attachments] : slots) {
            writeVarint(binary, indexOf(context.slots, slot, "timeline slot"), true);
            size_t count = 0;
            for (const auto& [placeholder, timelines] : attachments)
                count += std::count_if(timelines.begin(), timelines.end(), [](const auto& entry) { return !entry.second.empty(); });
            writeVarint(binary, count, true);
            for (const auto& [placeholder, timelines] : attachments) {
                const auto& attachment = context.attachment({skin, slot, placeholder});
                for (const auto& [name, keys] : timelines) {
                    if (keys.empty()) continue;
                    const auto type = attachmentTimelineTypeMap.at(name);
                    context.stringRef(binary, placeholder);
                    writeByte(binary, type);
                    writeVarint(binary, keys.size(), true);
                    if (type == ATTACHMENT_DEFORM) {
                        const size_t length = deformLength(attachment, context, slot);
                        writeVarint(binary, bezierCount(keys, 1), true);
                        for (size_t i = 0; i < keys.size(); ++i) {
                            const auto& key = keys[i];
                            writeFloat(binary, key.time);
                            if (i > 0) writeCurve(binary, keys[i - 1]);
                            writeVarint(binary, key.vertices.size(), true);
                            if (!key.vertices.empty()) {
                                if (key.int1 < 0 || static_cast<size_t>(key.int1) + key.vertices.size() > length)
                                    throw std::runtime_error("Deform key exceeds vertex count: " + placeholder);
                                writeVarint(binary, key.int1, true);
                                for (float value : key.vertices) writeFloat(binary, value);
                            }
                        }
                    } else {
                        if (attachment.type != AttachmentType_Mesh && attachment.type != AttachmentType_Linkedmesh &&
                            attachment.type != AttachmentType_Region)
                            throw std::runtime_error("Sequence timeline requires a textured attachment: " + placeholder);
                        for (const auto& key : keys) {
                            writeFloat(binary, key.time);
                            writeInt(binary, (static_cast<unsigned int>(key.int1) << 4) | key.sequenceMode);
                            writeFloat(binary, key.value1);
                        }
                    }
                }
            }
        }
    }
}

void writeDrawOrder(Binary& binary, const Timeline& keys, const Indices& slots) {
    writeVarint(binary, keys.size(), true);
    for (const auto& key : keys) {
        writeFloat(binary, key.time);
        std::map<int, int> offsets;
        std::set<int> destinations;
        for (const auto& [slot, offset] : key.offsets) {
            const int index = indexOf(slots, slot, "draw order slot");
            const auto destination = static_cast<int64_t>(index) + offset;
            if (destination < 0 || destination >= static_cast<int64_t>(slots.size()) ||
                !offsets.emplace(index, offset).second || !destinations.insert(static_cast<int>(destination)).second)
                throw std::runtime_error("Invalid draw order offset: " + slot);
        }
        writeVarint(binary, offsets.size(), true);
        for (const auto& [index, offset] : offsets) {
            writeVarint(binary, index, true);
            writeVarint(binary, offset, true);
        }
    }
}

bool hasAudio(const EventData& event) {
    return event.audioPath && !event.audioPath->empty();
}

void writeAnimation(Binary& binary, const Animation& animation, const WriteContext& context) {
    writeString(binary, animation.name);
    writeVarint(binary, 0, true); // Optional timeline capacity hint.
    writeGroups(binary, animation.slots, [&](Binary& out, const auto& name, auto count, const MultiTimeline& timelines) {
        writeVarint(out, indexOf(context.slots, name, "timeline slot"), true);
        writeVarint(out, count, true);
        writeTimelines(out, timelines, slotTimelineTypeMap,
            [&](SlotTimelineType type, const Timeline& keys) { writeSlotTimeline(out, type, keys, context); });
    });
    writeGroups(binary, animation.bones, [&](Binary& out, const auto& name, auto count, const MultiTimeline& timelines) {
        writeVarint(out, indexOf(context.bones, name, "timeline bone"), true);
        writeVarint(out, count, true);
        writeTimelines(out, timelines, boneTimelineTypeMap, [&](BoneTimelineType type, const Timeline& keys) {
            if (type == BONE_INHERIT) {
                for (const auto& key : keys) {
                    writeFloat(out, key.time);
                    writeByte(out, key.inherit);
                }
            } else {
                writeFloatTimeline(out, keys, type == BONE_TRANSLATE || type == BONE_SCALE || type == BONE_SHEAR ? 2 : 1);
            }
        });
    });
    auto constraintTimelines = [&](const std::map<std::string, Timeline>& timelines, ConstraintType type, auto writeKeys) {
        auto count = std::count_if(timelines.begin(), timelines.end(), [](const auto& entry) { return !entry.second.empty(); });
        writeVarint(binary, count, true);
        for (const auto& [name, keys] : timelines) {
            if (keys.empty()) continue;
            writeVarint(binary, context.constraintIndex(type, name), true);
            writeVarint(binary, keys.size(), true);
            writeKeys(keys);
        }
    };
    constraintTimelines(animation.ik, ConstraintType::Ik, [&](const Timeline& keys) { writeIkTimeline(binary, keys); });
    constraintTimelines(animation.transform, ConstraintType::Transform, [&](const Timeline& keys) { writeFloatTimeline(binary, keys, 6); });
    writeGroups(binary, animation.path, [&](Binary& out, const auto& name, auto count, const MultiTimeline& timelines) {
        writeVarint(out, context.constraintIndex(ConstraintType::Path, name), true);
        writeVarint(out, count, true);
        writeTimelines(out, timelines, pathTimelineTypeMap, [&](PathTimelineType type, const Timeline& keys) {
            writeFloatTimeline(out, keys, type == PATH_MIX ? 3 : 1);
        });
    });
    writeGroups(binary, animation.physics, [&](Binary& out, const auto& name, auto count, const MultiTimeline& timelines) {
        writeVarint(out, name.empty() ? 0 : context.constraintIndex(ConstraintType::Physics, name) + 1, true);
        writeVarint(out, count, true);
        writeTimelines(out, timelines, physicsTimelineTypeMap, [&](PhysicsTimelineType type, const Timeline& keys) {
            if (type == PHYSICS_RESET) {
                for (const auto& key : keys) writeFloat(out, key.time);
            } else {
                writeFloatTimeline(out, keys, 1);
            }
        });
    });
    const std::map<std::string, int> sliderTypes{{"time", 0}, {"mix", 1}};
    writeGroups(binary, animation.slider, [&](Binary& out, const auto& name, auto count, const MultiTimeline& timelines) {
        writeVarint(out, context.constraintIndex(ConstraintType::Slider, name), true);
        writeVarint(out, count, true);
        writeTimelines(out, timelines, sliderTypes, [&](int, const Timeline& keys) { writeFloatTimeline(out, keys, 1); });
    });
    writeAttachmentTimelines(binary, animation, context);
    writeDrawOrder(binary, animation.drawOrder, context.slots);
    const auto folderCount = std::count_if(animation.drawOrderFolders.begin(), animation.drawOrderFolders.end(),
        [](const DrawOrderFolder& folder) { return !folder.slots.empty() && !folder.keys.empty(); });
    writeVarint(binary, folderCount, true);
    for (const auto& folder : animation.drawOrderFolders) {
        if (folder.slots.empty() || folder.keys.empty()) continue;
        Indices slots;
        writeVarint(binary, folder.slots.size(), true);
        for (const auto& slot : folder.slots) {
            if (!slots.emplace(slot, slots.size()).second) throw std::runtime_error("Duplicate draw order folder slot: " + slot);
            writeVarint(binary, indexOf(context.slots, slot, "draw order folder slot"), true);
        }
        writeDrawOrder(binary, folder.keys, slots);
    }
    writeVarint(binary, animation.events.size(), true);
    for (const auto& key : animation.events) {
        const int index = indexOf(context.events, key.str1, "event");
        const auto& event = context.data.events[index];
        writeFloat(binary, key.time);
        writeVarint(binary, index, true);
        writeVarint(binary, key.int1, false);
        writeFloat(binary, key.value1);
        writeString(binary, key.str2 != event.stringValue ? key.str2 : std::nullopt);
        if (hasAudio(event)) {
            writeFloat(binary, key.value2);
            writeFloat(binary, key.value3);
        }
    }
    if (context.data.nonessential) writeColor(binary, animation.color.value_or(Color{}));
}

} // namespace

Binary writeBinaryData(const SkeletonData& data) {
    const WriteContext context(data);
    Binary binary;
    writeInt(binary, std::bit_cast<int32_t>(static_cast<uint32_t>(data.hash >> 32)));
    writeInt(binary, std::bit_cast<int32_t>(static_cast<uint32_t>(data.hash)));
    writeString(binary, data.version);
    for (float value : {data.x, data.y, data.width, data.height, data.referenceScale}) writeFloat(binary, value);
    writeBoolean(binary, data.nonessential);
    if (data.nonessential) {
        writeFloat(binary, data.fps);
        writeString(binary, data.imagesPath);
        writeString(binary, data.audioPath);
    }
    writeVarint(binary, context.strings.size(), true);
    for (const auto& [string, index] : context.strings) writeString(binary, string);

    writeVarint(binary, data.bones.size(), true);
    for (size_t i = 0; i < data.bones.size(); ++i) {
        const auto& bone = data.bones[i];
        writeString(binary, bone.name);
        if (i != 0) {
            int parent = indexOf(context.bones, bone.parent, "parent bone");
            if (static_cast<size_t>(parent) >= i) throw std::runtime_error("Parent bone must precede its child");
            writeVarint(binary, parent, true);
        } else if (bone.parent) {
            throw std::runtime_error("The first bone must be the root");
        }
        for (float value : {bone.rotation, bone.x, bone.y, bone.scaleX, bone.scaleY, bone.shearX, bone.shearY})
            writeFloat(binary, value);
        writeByte(binary, bone.inherit);
        writeFloat(binary, bone.length);
        writeBoolean(binary, bone.skinRequired);
        if (data.nonessential) {
            writeColor(binary, bone.color.value_or(boneColor));
            writeString(binary, bone.icon);
            writeFloat(binary, bone.iconSize);
            writeFloat(binary, bone.iconRotation);
            writeBoolean(binary, bone.visible);
        }
    }

    writeVarint(binary, data.slots.size(), true);
    for (const auto& slot : data.slots) {
        writeString(binary, slot.name);
        writeVarint(binary, indexOf(context.bones, slot.bone, "slot bone"), true);
        writeColor(binary, slot.color.value_or(Color{}));
        if (slot.darkColor) {
            writeByte(binary, 0);
            writeColor(binary, *slot.darkColor, false);
        } else {
            writeInt(binary, -1);
        }
        context.stringRef(binary, slot.attachmentName);
        writeVarint(binary, slot.blendMode, true);
        if (data.nonessential) writeBoolean(binary, slot.visible);
    }

    writeVarint(binary, context.constraints.size(), true);
    for (const auto& constraint : context.constraints) {
        std::visit([&](auto* value) {
            writeString(binary, value->name);
            writeByte(binary, static_cast<unsigned char>(constraint.index()));
            writeConstraint(binary, *value, context);
        }, constraint);
    }

    const bool hasDefault = !context.skins.empty() && context.skins.front()->name == "default";
    if (hasDefault) writeSkin(binary, *context.skins.front(), context, true);
    else writeVarint(binary, 0, true);
    writeVarint(binary, context.skins.size() - hasDefault, true);
    for (size_t i = hasDefault; i < context.skins.size(); ++i) writeSkin(binary, *context.skins[i], context, false);

    writeVarint(binary, data.events.size(), true);
    for (const auto& event : data.events) {
        writeString(binary, event.name);
        writeVarint(binary, event.intValue, false);
        writeFloat(binary, event.floatValue);
        writeString(binary, event.stringValue);
        writeString(binary, event.audioPath);
        if (hasAudio(event)) {
            writeFloat(binary, event.volume);
            writeFloat(binary, event.balance);
        }
    }
    writeVarint(binary, data.animations.size(), true);
    for (const auto& animation : data.animations) writeAnimation(binary, animation, context);
    for (const auto& constraint : context.constraints) {
        if (auto slider = std::get_if<const SliderData*>(&constraint))
            writeVarint(binary, indexOf(context.animations, (*slider)->animation, "slider animation"), true);
    }
    return binary;
}

} // namespace spine43
