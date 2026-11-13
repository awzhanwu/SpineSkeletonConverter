#include "SkeletonData43.h"

namespace spine43 {
namespace {

int readCount(DataInput* input, size_t bytesPerItem = 1) {
    const int value = readVarint(input, true);
    if (value < 0 || static_cast<size_t>(value) > static_cast<size_t>(input->end - input->cursor) / bytesPerItem)
        throw std::runtime_error("Invalid count in Spine 4.3 binary");
    return value;
}

template <class Enum>
Enum readEnum(DataInput* input, size_t count) {
    const auto value = readByte(input);
    if (value >= count) throw std::runtime_error("Invalid enum value in Spine 4.3 binary");
    return static_cast<Enum>(value);
}

struct ConstraintRef {
    ConstraintType type;
    std::string name;
};

const std::string& readConstraintName(DataInput* input, const std::vector<ConstraintRef>& constraints,
                                     ConstraintType type) {
    const auto& ref = constraints.at(readVarint(input, true));
    if (ref.type != type) throw std::runtime_error("Unexpected constraint type for timeline: " + ref.name);
    return ref.name;
}

Sequence readSequence(DataInput* input) {
    Sequence sequence; 
    sequence.count = readVarint(input, true);
    sequence.start = readVarint(input, true);
    sequence.digits = readVarint(input, true);
    sequence.setupIndex = readVarint(input, true);
    return sequence;
}

void readFloatArray(DataInput* input, int n, std::vector<float>& array) {
    if (n < 0 || n > (input->end - input->cursor) / 4)
        throw std::runtime_error("Invalid float array in Spine 4.3 binary");
    array.resize(n, 0);
    for (int i = 0; i < n; i++)
        array[i] = readFloat(input);
}

void readShortArray(DataInput* input, int n, std::vector<unsigned short>& array) {
    if (n < 0 || n > input->end - input->cursor)
        throw std::runtime_error("Invalid index array in Spine 4.3 binary");
    array.resize(n, 0);
    for (int i = 0; i < n; i++)
        array[i] = (short) readVarint(input, true); 
}

int readVertices(DataInput* input, std::vector<float>& vertices, bool weighted) {
    int vertexCount = readCount(input, weighted ? 1 : 8);
    if (!weighted) {
        readFloatArray(input, vertexCount << 1, vertices);
    } else {
        int entries = readCount(input);
        for (int i = 0; i < vertexCount; i++) {
            int boneCount = readCount(input, 13);
            entries -= boneCount + 1;
            if (boneCount == 0 || entries < 0) throw std::runtime_error("Invalid weighted vertex entries");
            vertices.push_back(boneCount);
            for (int ii = 0; ii < boneCount; ii++) {
                vertices.push_back(readVarint(input, true));
                vertices.push_back(readFloat(input));
                vertices.push_back(readFloat(input));
                vertices.push_back(readFloat(input));
            }
        }
        if (entries != 0) throw std::runtime_error("Invalid weighted vertex entry count");
    }
    return vertexCount; 
}

void readCurve(DataInput* input, TimelineFrame& frame, int timelineCount) {
    for (int i = 0; i < timelineCount * 4; i++) {
        frame.curve.push_back(readFloat(input));
    }
}

Timeline readTimeline(DataInput* input, int frameCount, int valueNum) {
    if (frameCount <= 0) throw std::runtime_error("Empty curve timeline");
    Timeline timeline;
    for (int i = 0; i < frameCount; ++i) {
        TimelineFrame frame;
        frame.time = readFloat(input);
        const std::array values{&frame.value1, &frame.value2, &frame.value3, &frame.value4, &frame.value5, &frame.value6};
        for (int j = 0; j < valueNum; ++j) *values[j] = readFloat(input);
        if (i != 0) {
            auto& previous = timeline.back();
            previous.curveType = readEnum<CurveType>(input, 3);
            if (previous.curveType == CURVE_BEZIER) readCurve(input, previous, valueNum);
        }
        timeline.push_back(std::move(frame));
    }
    return timeline;
}

Skin readSkin(DataInput* input, bool defaultSkin, SkeletonData* skeletonData, const std::vector<ConstraintRef>& constraints) {
    Skin skin; 
    int slotCount = 0; 
    if (defaultSkin) {
        slotCount = readVarint(input, true);
        skin.name = "default";
    } else {
        skin.name = readString(input).value();
        if (skeletonData->nonessential) {
            Color color = readColor(input);
            if (color != skinColor) skin.color = color;
        }
        for (int i = 0, n = readVarint(input, true); i < n; i++) {
            skin.bones.push_back(skeletonData->bones.at(readVarint(input, true)).name.value());
        }
        const std::array groups{&skin.ik, &skin.path, &skin.transform, &skin.physics, &skin.slider};
        for (int i = 0, n = readCount(input); i < n; i++) {
            const auto& ref = constraints.at(readVarint(input, true));
            groups[static_cast<size_t>(ref.type)]->push_back(ref.name);
        }
        slotCount = readVarint(input, true);
    }
    for (int i = 0; i < slotCount; i++) {
        std::string slotName = skeletonData->slots.at(readVarint(input, true)).name.value();
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            std::string attachmentName = readStringRef(input, skeletonData).value();
            Attachment attachment;
            int flags = readByte(input);
            attachment.name = (flags & 8) != 0 ? readStringRef(input, skeletonData).value() : attachmentName;
            attachment.type = static_cast<AttachmentType>(flags & 0x7);
            switch (attachment.type) {
                case AttachmentType_Region: {
                    RegionAttachment region; 
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) region.color = readColor(input);
                    if ((flags & 64) != 0) region.sequence = readSequence(input);
                    if ((flags & 128) != 0) region.rotation = readFloat(input);
                    region.x = readFloat(input);
                    region.y = readFloat(input);
                    region.scaleX = readFloat(input);
                    region.scaleY = readFloat(input);
                    region.width = readFloat(input);
                    region.height = readFloat(input);
                    attachment.data = region;
                    break; 
                }
                case AttachmentType_Boundingbox: {
                    BoundingboxAttachment box;
                    attachment.path = attachment.name; 
                    int vertexCount = readVertices(input, box.vertices, (flags & 16) != 0);
                    box.vertexCount = vertexCount;
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != boxColor) box.color = color;
                    }
                    attachment.data = box;
                    break; 
                }
                case AttachmentType_Mesh: {
                    MeshAttachment mesh; 
                    mesh.width = mesh.height = 0;
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) mesh.color = readColor(input);
                    if ((flags & 64) != 0) mesh.sequence = readSequence(input);
                    mesh.hullLength = readVarint(input, true);
                    int vertexCount = readVertices(input, mesh.vertices, (flags & 128) != 0);
                    readFloatArray(input, vertexCount << 1, mesh.uvs);
                    readShortArray(input, (vertexCount * 2 - mesh.hullLength - 2) * 3, mesh.triangles);
                    for (int index = 0, count = readCount(input); index < count; ++index)
                        skeletonData->slots.at(readVarint(input, true));
                    if (skeletonData->nonessential) {
                        readShortArray(input, readVarint(input, true), mesh.edges);
                        mesh.width = readFloat(input);
                        mesh.height = readFloat(input);
                    }
                    attachment.data = mesh;
                    break; 
                }
                case AttachmentType_Linkedmesh: {
                    LinkedmeshAttachment linkedMesh; 
                    linkedMesh.width = linkedMesh.height = 0;
                    attachment.path = (flags & 16) != 0 ? readStringRef(input, skeletonData).value() : attachment.name;
                    if ((flags & 32) != 0) linkedMesh.color = readColor(input);
                    if ((flags & 64) != 0) linkedMesh.sequence = readSequence(input);
                    linkedMesh.timelines = (flags & 128) != 0 ? 1 : 0;
                    linkedMesh.sourceSlot = skeletonData->slots.at(readVarint(input, true)).name;
                    if (linkedMesh.sourceSlot == slotName) linkedMesh.sourceSlot.reset();
                    linkedMesh.skinIndex = readVarint(input, true);
                    linkedMesh.parentMesh = readStringRef(input, skeletonData).value();
                    if (skeletonData->nonessential) {
                        linkedMesh.width = readFloat(input);
                        linkedMesh.height = readFloat(input);
                    }
                    attachment.data = linkedMesh;
                    break; 
                }
                case AttachmentType_Path: {
                    PathAttachment path; 
                    attachment.path = attachment.name; 
                    path.closed = (flags & 16) != 0;
                    path.constantSpeed = (flags & 32) != 0;
                    int vertexCount = readVertices(input, path.vertices, (flags & 64) != 0);
                    path.vertexCount = vertexCount;
                    readFloatArray(input, vertexCount / 3, path.lengths);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != transparent) path.color = color;
                    }
                    attachment.data = path;
                    break; 
                }
                case AttachmentType_Point: {
                    PointAttachment point; 
                    attachment.path = attachment.name; 
                    point.rotation = readFloat(input);
                    point.x = readFloat(input);
                    point.y = readFloat(input);
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != pointColor) point.color = color;
                    }
                    attachment.data = point;
                    break; 
                }
                case AttachmentType_Clipping: {
                    ClippingAttachment clipping; 
                    attachment.path = attachment.name; 
                    clipping.endSlot = skeletonData->slots.at(readVarint(input, true)).name;
                    clipping.convex = (flags & 32) != 0;
                    clipping.inverse = (flags & 64) != 0;
                    int vertexCount = readVertices(input, clipping.vertices, (flags & 16) != 0);
                    clipping.vertexCount = vertexCount;
                    if (skeletonData->nonessential) {
                        Color color = readColor(input);
                        if (color != transparent) clipping.color = color;
                    }
                    attachment.data = clipping;
                    break;
                }
                default:
                    throw std::runtime_error("Invalid attachment type in Spine 4.3 binary");
            }
            skin.attachments[slotName][attachmentName] = attachment;
        }
    }
    return skin;
}

Timeline readDrawOrderKeys(DataInput* input, const std::vector<std::string>& slots) {
    Timeline timeline;
    for (int i = 0, n = readCount(input, 5); i < n; ++i) {
        TimelineFrame key;
        key.time = readFloat(input);
        for (int j = 0, count = readCount(input, 2); j < count; ++j) {
            const auto& slot = slots.at(readVarint(input, true));
            key.offsets.emplace_back(slot, readVarint(input, true));
        }
        timeline.push_back(std::move(key));
    }
    return timeline;
}

Animation readAnimation(DataInput* input, SkeletonData* skeletonData, const std::vector<ConstraintRef>& constraints) {
    Animation animation; 
    animation.name = readString(input).value();
    readVarint(input, true); // Timeline capacity hint.
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string slotName = skeletonData->slots.at(readVarint(input, true)).name.value();
        MultiTimeline slotTimeline; 
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            SlotTimelineType timelineType = static_cast<SlotTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            switch (timelineType) {
                case SlotTimelineType::SLOT_ATTACHMENT: {
                    Timeline timeline;
                    for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = readFloat(input);
                        frame.str1 = readStringRef(input, skeletonData);
                        timeline.push_back(frame);
                    }
                    slotTimeline["attachment"] = timeline;
                    break; 
                }
                case SlotTimelineType::SLOT_RGBA: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    float time = readFloat(input);
                    Color color = readColor(input);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = time; 
                        frame.color1 = color; 
                        time = readFloat(input);
                        color = readColor(input);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break; 
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 4);
                                break; 
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = color;
                    timeline.push_back(frame);
                    slotTimeline["rgba"] = timeline;
                    break; 
                }
                case SlotTimelineType::SLOT_RGB: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    float time = readFloat(input);
                    Color color = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = time; 
                        frame.color1 = color; 
                        time = readFloat(input);
                        color = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break; 
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 3);
                                break; 
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = color;
                    timeline.push_back(frame);
                    slotTimeline["rgb"] = timeline;
                    break; 
                }
                case SlotTimelineType::SLOT_RGBA2: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    float time = readFloat(input);
                    Color light = readColor(input);
                    Color dark = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = time; 
                        frame.color1 = light; 
                        frame.color2 = dark; 
                        time = readFloat(input);
                        light = readColor(input);
                        dark = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break; 
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 7);
                                break; 
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = light;
                    frame.color2 = dark;
                    timeline.push_back(frame);
                    slotTimeline["rgba2"] = timeline;
                    break; 
                }
                case SlotTimelineType::SLOT_RGB2: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    float time = readFloat(input);
                    Color light = readColor(input, false);
                    Color dark = readColor(input, false);
                    for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = time; 
                        frame.color1 = light; 
                        frame.color2 = dark; 
                        time = readFloat(input);
                        light = readColor(input, false);
                        dark = readColor(input, false);
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break; 
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 6);
                                break; 
                            }
                        }
                        timeline.push_back(frame);
                    }
                    TimelineFrame frame;
                    frame.time = time;
                    frame.color1 = light;
                    frame.color2 = dark;
                    timeline.push_back(frame);
                    slotTimeline["rgb2"] = timeline;
                    break; 
                }
                case SlotTimelineType::SLOT_ALPHA: {
                    Timeline timeline;
                    int bezierCount = readVarint(input, true);
                    float time = readFloat(input);
                    float alpha = readByte(input) / 255.0f; 
                    for (int frameIndex = 0; ; frameIndex++) {
                        TimelineFrame frame; 
                        frame.time = time; 
                        frame.value1 = alpha; 
                        if (frameIndex == frameCount - 1) {
                            timeline.push_back(frame);
                            break;
                        }
                        time = readFloat(input);
                        alpha = readByte(input) / 255.0f; 
                        switch (readSByte(input)) {
                            case CURVE_STEPPED: {
                                frame.curveType = CurveType::CURVE_STEPPED;
                                break; 
                            }
                            case CURVE_BEZIER: {
                                frame.curveType = CurveType::CURVE_BEZIER;
                                readCurve(input, frame, 1);
                                break; 
                            }
                        }
                        timeline.push_back(frame);
                    }
                    slotTimeline["alpha"] = timeline;
                    break;
                }
            }
        }
        animation.slots[slotName] = slotTimeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string boneName = skeletonData->bones.at(readVarint(input, true)).name.value();
        MultiTimeline boneTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            BoneTimelineType timelineType = static_cast<BoneTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            if (timelineType == BONE_INHERIT) {
                Timeline timeline; 
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame; 
                    frame.time = readFloat(input);
                    frame.inherit = (Inherit) readByte(input);
                    timeline.push_back(frame);
                }
                boneTimeline["inherit"] = timeline;
                continue; 
            }
            int bezierCount = readVarint(input, true);
            switch (timelineType) {
                case BONE_ROTATE: {
                    boneTimeline["rotate"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_TRANSLATE: {
                    boneTimeline["translate"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_TRANSLATEX: {
                    boneTimeline["translatex"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_TRANSLATEY: {
                    boneTimeline["translatey"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SCALE: {
                    boneTimeline["scale"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_SCALEX: {
                    boneTimeline["scalex"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SCALEY: {
                    boneTimeline["scaley"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SHEAR: {
                    boneTimeline["shear"] = readTimeline(input, frameCount, 2);
                    break;
                }
                case BONE_SHEARX: {
                    boneTimeline["shearx"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case BONE_SHEARY: {
                    boneTimeline["sheary"] = readTimeline(input, frameCount, 1);
                    break;
                }
            }
        }
        animation.bones[boneName] = boneTimeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string ikName = readConstraintName(input, constraints, ConstraintType::Ik);
        int frameCount = readVarint(input, true);
        int bezierCount = readVarint(input, true);
        Timeline timeline;
        int flags = readByte(input);
        float time = readFloat(input);
        float mix = (flags & 1) != 0 ? ((flags & 2) != 0 ? readFloat(input) : 1) : 0; 
        float softness = (flags & 4) != 0 ? readFloat(input) : 0;
        bool bendPositive = (flags & 8) != 0;
        bool compress = (flags & 16) != 0;
        bool stretch = (flags & 32) != 0;
        for (int frameIndex = 0; frameIndex < frameCount - 1; frameIndex++) {
            TimelineFrame frame; 
            frame.time = time; 
            frame.value1 = mix; 
            frame.value2 = softness; 
            frame.bendPositive = bendPositive;
            frame.compress = compress;
            frame.stretch = stretch;
            flags = readByte(input);
            time = readFloat(input);
            mix = (flags & 1) != 0 ? (flags & 2) != 0 ? readFloat(input) : 1 : 0;
            softness = (flags & 4) != 0 ? readFloat(input) : 0;
            bendPositive = (flags & 8) != 0;
            compress = (flags & 16) != 0;
            stretch = (flags & 32) != 0;
            if ((flags & 64) != 0) {
                frame.curveType = CurveType::CURVE_STEPPED;
            } else if ((flags & 128) != 0) {
                frame.curveType = CurveType::CURVE_BEZIER;
                readCurve(input, frame, 2);
            }
            timeline.push_back(frame);
        }
        TimelineFrame frame;
        frame.time = time;
        frame.value1 = mix;
        frame.value2 = softness;
        frame.bendPositive = bendPositive;
        frame.compress = compress;
        frame.stretch = stretch;
        timeline.push_back(frame);
        animation.ik[ikName] = timeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        const auto& name = readConstraintName(input, constraints, ConstraintType::Transform);
        const int frameCount = readCount(input);
        readVarint(input, true); // Bezier count.
        animation.transform[name] = readTimeline(input, frameCount, 6);
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string pathName = readConstraintName(input, constraints, ConstraintType::Path);
        MultiTimeline pathTimeline; 
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            PathTimelineType timelineType = static_cast<PathTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            int bezierCount = readVarint(input, true);
            switch (timelineType) {
                case PATH_POSITION: {
                    pathTimeline["position"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PATH_SPACING: {
                    pathTimeline["spacing"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PATH_MIX: {
                    pathTimeline["mix"] = readTimeline(input, frameCount, 3);
                    break; 
                }
            }
        }
        animation.path[pathName] = pathTimeline;
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        int index = readVarint(input, true) - 1;
        std::string physicsName;
        if (index != -1) {
            const auto& ref = constraints.at(index);
            if (ref.type != ConstraintType::Physics) throw std::runtime_error("Invalid physics timeline constraint");
            physicsName = ref.name;
        }
        MultiTimeline physicsTimeline;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            PhysicsTimelineType timelineType = static_cast<PhysicsTimelineType>(readByte(input));
            int frameCount = readVarint(input, true);
            if (timelineType == PHYSICS_RESET) {
                Timeline timeline; 
                for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    TimelineFrame frame; 
                    frame.time = readFloat(input);
                    timeline.push_back(frame);
                }
                physicsTimeline["reset"] = timeline;
                continue; 
            }
            int bezierCount = readVarint(input, true);
            switch (timelineType) {
                case PHYSICS_INERTIA: {
                    physicsTimeline["inertia"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_STRENGTH: {
                    physicsTimeline["strength"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_DAMPING: {
                    physicsTimeline["damping"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_MASS: {
                    physicsTimeline["mass"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_WIND: {
                    physicsTimeline["wind"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_GRAVITY: {
                    physicsTimeline["gravity"] = readTimeline(input, frameCount, 1);
                    break;
                }
                case PHYSICS_MIX: {
                    physicsTimeline["mix"] = readTimeline(input, frameCount, 1);
                    break;
                }
            }
        }
        animation.physics[physicsName] = physicsTimeline;
    }
    for (int i = 0, n = readCount(input); i < n; ++i) {
        const auto& name = readConstraintName(input, constraints, ConstraintType::Slider);
        for (int j = 0, count = readCount(input); j < count; ++j) {
            const auto type = readEnum<unsigned char>(input, 2);
            const int frames = readCount(input);
            readVarint(input, true); // Bezier count.
            animation.slider[name][type == 0 ? "time" : "mix"] = readTimeline(input, frames, 1);
        }
    }
    for (int i = 0, n = readVarint(input, true); i < n; i++) {
        std::string skinName = skeletonData->skins.at(readVarint(input, true)).name;
        for (int ii = 0, nn = readVarint(input, true); ii < nn; ii++) {
            std::string slotName = skeletonData->slots.at(readVarint(input, true)).name.value();
            for (int iii = 0, nnn = readVarint(input, true); iii < nnn; iii++) {
                std::string attachmentName = readStringRef(input, skeletonData).value();
                auto& attachmentTimeline = animation.attachments[skinName][slotName][attachmentName];
                AttachmentTimelineType timelineType = static_cast<AttachmentTimelineType>(readByte(input));
                int frameCount = readVarint(input, true);
                switch (timelineType) {
                    case ATTACHMENT_DEFORM: {
                        int bezierCount = readVarint(input, true);
                        float time = readFloat(input);
                        for (int frameIndex = 0; ; frameIndex++) {
                            TimelineFrame frame; 
                            frame.time = time; 
                            size_t end = (size_t) readVarint(input, true);
                            if (end != 0) {
                                size_t start = (size_t) readVarint(input, true);
                                frame.int1 = start;
                                end += start;
                                for (size_t v = start; v < end; v++)
                                    frame.vertices.push_back(readFloat(input));
                            }
                            if (frameIndex == frameCount - 1) {
                                attachmentTimeline["deform"].push_back(frame);
                                break; 
                            }
                            time = readFloat(input);
                            switch (readSByte(input)) {
                                case CURVE_STEPPED: {
                                    frame.curveType = CurveType::CURVE_STEPPED;
                                    break; 
                                }
                                case CURVE_BEZIER: {
                                    frame.curveType = CurveType::CURVE_BEZIER;
                                    readCurve(input, frame, 1);
                                    break;
                                }
                            }
                            attachmentTimeline["deform"].push_back(frame);
                        }
                        break; 
                    }
                    case ATTACHMENT_SEQUENCE: {
                        for (int frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                            TimelineFrame frame; 
                            frame.time = readFloat(input);
                            int modeAndIndex = readInt(input);
                            frame.sequenceMode = (SequenceMode) (modeAndIndex & 0xf);
                            frame.int1 = modeAndIndex >> 4; 
                            frame.value1 = readFloat(input);
                            attachmentTimeline["sequence"].push_back(frame);
                        }
                        break; 
                    }
                }
            }
        }
    }
    std::vector<std::string> slots;
    for (const auto& slot : skeletonData->slots) slots.push_back(slot.name.value());
    animation.drawOrder = readDrawOrderKeys(input, slots);
    for (int i = 0, n = readCount(input); i < n; ++i) {
        DrawOrderFolder folder;
        for (int j = 0, count = readCount(input); j < count; ++j)
            folder.slots.push_back(slots.at(readVarint(input, true)));
        folder.keys = readDrawOrderKeys(input, folder.slots);
        animation.drawOrderFolders.push_back(std::move(folder));
    }
    int eventCount = readVarint(input, true);
    for (int i = 0; i < eventCount; i++) {
        TimelineFrame frame; 
        frame.time = readFloat(input);
        int eventIndex = readVarint(input, true);
        const EventData& eventData = skeletonData->events.at(eventIndex);
        frame.str1 = eventData.name;
        frame.int1 = readVarint(input, false);
        frame.value1 = readFloat(input);
        OptStr str = readString(input);
        if (str.has_value()) frame.str2 = str.value();
        else frame.str2 = eventData.stringValue;
        if (eventData.audioPath && !eventData.audioPath->empty()) {
            frame.value2 = readFloat(input);
            frame.value3 = readFloat(input);
        }
        animation.events.push_back(frame);
    }
    if (skeletonData->nonessential) {
        const auto color = readColor(input);
        if (color != Color{}) animation.color = color;
    }
    return animation; 
}

std::vector<std::string> readConstraintBones(DataInput* input, const SkeletonData& data) {
    std::vector<std::string> bones;
    for (int i = 0, count = readCount(input); i < count; ++i)
        bones.push_back(data.bones.at(readVarint(input, true)).name.value());
    return bones;
}

void readFlaggedFloats(DataInput* input, std::initializer_list<float*> values) {
    int flags = readByte(input);
    for (float* value : values) {
        *value = (flags & 1) ? readFloat(input) : 0.0f;
        flags >>= 1;
    }
}

std::vector<ConstraintRef> readConstraints(DataInput* input, SkeletonData& data) {
    std::vector<ConstraintRef> constraints;
    for (int i = 0, count = readCount(input); i < count; ++i) {
        auto name = readString(input).value();
        auto type = readEnum<ConstraintType>(input, constraintNames.size());
        constraints.push_back({type, name});
        switch (type) {
            case ConstraintType::Ik: {
                IKConstraintData ik;
                ik.name = name;
                ik.order = i;
                ik.bones = readConstraintBones(input, data);
                ik.target = data.bones.at(readVarint(input, true)).name;
                const auto flags = readByte(input);
                ik.skinRequired = flags & 1;
                if (flags & 2) ik.scaleY = readEnum<ScaleYMode>(input, scaleYNames.size());
                ik.uniform = ik.scaleY == ScaleYMode::Uniform;
                ik.bendPositive = !(flags & 4);
                ik.compress = flags & 8;
                ik.stretch = flags & 16;
                ik.mix = (flags & 32) ? ((flags & 64) ? readFloat(input) : 1.0f) : 0.0f;
                if (flags & 128) ik.softness = readFloat(input);
                data.ikConstraints.push_back(std::move(ik));
                break;
            }
            case ConstraintType::Transform: {
                TransformConstraintData transform;
                transform.name = name;
                transform.order = i;
                transform.bones = readConstraintBones(input, data);
                transform.target = data.bones.at(readVarint(input, true)).name;
                const auto flags = readByte(input);
                auto& mapping = transform.mapping.emplace();
                transform.skinRequired = flags & 1;
                mapping.localSource = flags & 2;
                mapping.localTarget = flags & 4;
                mapping.additive = flags & 8;
                mapping.clamp = flags & 16;
                transform.local = mapping.localSource && mapping.localTarget;
                transform.relative = mapping.additive;
                for (int j = 0; j < (flags >> 5); ++j) {
                    TransformFromProperty from{readEnum<TransformProperty>(input, propertyNames.size()), readFloat(input), {}};
                    for (int k = 0, count = readByte(input); k < count; ++k)
                        from.to.push_back({readEnum<TransformProperty>(input, propertyNames.size()),
                                           readFloat(input), readFloat(input), readFloat(input)});
                    mapping.properties.push_back(std::move(from));
                }
                readFlaggedFloats(input, {&transform.offsetRotation, &transform.offsetX, &transform.offsetY,
                                          &transform.offsetScaleX, &transform.offsetScaleY, &transform.offsetShearY});
                readFlaggedFloats(input, {&transform.mixRotate, &transform.mixX, &transform.mixY,
                                          &transform.mixScaleX, &transform.mixScaleY, &transform.mixShearY});
                data.transformConstraints.push_back(std::move(transform));
                break;
            }
            case ConstraintType::Path: {
                PathConstraintData path;
                path.name = name;
                path.order = i;
                path.bones = readConstraintBones(input, data);
                path.target = data.slots.at(readVarint(input, true)).name;
                const auto flags = readByte(input);
                path.skinRequired = flags & 1;
                path.positionMode = static_cast<PositionMode>((flags >> 1) & 1);
                path.spacingMode = static_cast<SpacingMode>((flags >> 2) & 3);
                path.rotateMode = static_cast<RotateMode>((flags >> 4) & 3);
                if (flags & 128) path.offsetRotation = readFloat(input);
                path.position = readFloat(input);
                path.spacing = readFloat(input);
                path.mixRotate = readFloat(input);
                path.mixX = readFloat(input);
                path.mixY = readFloat(input);
                data.pathConstraints.push_back(std::move(path));
                break;
            }
            case ConstraintType::Physics: {
                PhysicsConstraintData physics;
                physics.name = name;
                physics.order = i;
                physics.bone = data.bones.at(readVarint(input, true)).name;
                auto flags = readByte(input);
                physics.skinRequired = flags & 1;
                if (flags & 2) physics.x = readFloat(input);
                if (flags & 4) physics.y = readFloat(input);
                if (flags & 8) physics.rotate = readFloat(input);
                if (flags & 16) {
                    physics.scaleX = readFloat(input);
                    if (physics.scaleX < -2) {
                        physics.scaleY = ScaleYMode::Volume;
                        physics.scaleX = -2 - physics.scaleX;
                    } else if (physics.scaleX < 0) {
                        physics.scaleY = ScaleYMode::Uniform;
                        physics.scaleX = -1 - physics.scaleX;
                    }
                }
                if (flags & 32) physics.shearX = readFloat(input);
                if (flags & 64) physics.limit = readFloat(input);
                physics.fps = readByte(input);
                physics.inertia = readFloat(input);
                physics.strength = readFloat(input);
                physics.damping = readFloat(input);
                if (flags & 128) physics.mass = 1.0f / readFloat(input);
                physics.wind = readFloat(input);
                physics.gravity = readFloat(input);
                flags = readByte(input);
                physics.inertiaGlobal = flags & 1;
                physics.strengthGlobal = flags & 2;
                physics.dampingGlobal = flags & 4;
                physics.massGlobal = flags & 8;
                physics.windGlobal = flags & 16;
                physics.gravityGlobal = flags & 32;
                physics.mixGlobal = flags & 64;
                if (flags & 128) physics.mix = readFloat(input);
                data.physicsConstraints.push_back(std::move(physics));
                break;
            }
            case ConstraintType::Slider: {
                SliderData slider;
                slider.name = name;
                slider.order = i;
                const auto flags = readByte(input);
                slider.skinRequired = flags & 1;
                slider.loop = flags & 2;
                slider.additive = flags & 4;
                if (flags & 8) {
                    float& timeOrMax = data.nonessential && (flags & 64) ? slider.max : slider.time;
                    timeOrMax = readFloat(input);
                }
                slider.mix = (flags & 16) ? ((flags & 32) ? readFloat(input) : 1.0f) : 0.0f;
                if (flags & 64) {
                    slider.local = flags & 128;
                    slider.bone = data.bones.at(readVarint(input, true)).name;
                    slider.from = readFloat(input);
                    slider.property = readEnum<TransformProperty>(input, propertyNames.size());
                    slider.to = readFloat(input);
                    slider.scale = readFloat(input);
                }
                data.sliders.push_back(std::move(slider));
                break;
            }
        }
    }
    return constraints;
}

SkeletonData readNativeBinaryData(const Binary& binary) {
    if (binary.empty()) throw std::runtime_error("Empty skeleton binary");
    SkeletonData skeletonData;
    DataInput input; 
    input.cursor = binary.data(); 
    input.end = binary.data() + binary.size();

    uint64_t highHash = static_cast<uint32_t>(readInt(&input));
    uint64_t lowHash = static_cast<uint32_t>(readInt(&input));
    skeletonData.hash = highHash << 32 | (lowHash & 0xffffffff);
    skeletonData.version = readString(&input).value();

    skeletonData.x = readFloat(&input); 
    skeletonData.y = readFloat(&input);
    skeletonData.width = readFloat(&input);
    skeletonData.height = readFloat(&input);
    skeletonData.referenceScale = readFloat(&input); 

    skeletonData.nonessential = readBoolean(&input); 

    if (skeletonData.nonessential) {
        skeletonData.fps = readFloat(&input); 
        skeletonData.imagesPath = readString(&input);
        skeletonData.audioPath = readString(&input);
    }

    int numStrings = readCount(&input);
    for (int i = 0; i < numStrings; i++)
        skeletonData.strings.push_back(readString(&input).value());
    
    /* Bones */
    int numBones = readCount(&input, 35);
    for (int i = 0; i < numBones; i++) {
        BoneData boneData; 
        boneData.name = readString(&input); 
        if (i != 0) boneData.parent = skeletonData.bones.at(readVarint(&input, true)).name;
        boneData.rotation = readFloat(&input);
        boneData.x = readFloat(&input);
        boneData.y = readFloat(&input);
        boneData.scaleX = readFloat(&input);
        boneData.scaleY = readFloat(&input);
        boneData.shearX = readFloat(&input);
        boneData.shearY = readFloat(&input);
        boneData.inherit = readEnum<Inherit>(&input, inheritMap.size());
        boneData.length = readFloat(&input);
        boneData.skinRequired = readBoolean(&input);
        if (skeletonData.nonessential) {
            Color color = readColor(&input);
            if (color != boneColor) boneData.color = color;
            boneData.icon = readString(&input);
            boneData.iconSize = readFloat(&input);
            boneData.iconRotation = readFloat(&input);
            boneData.visible = readBoolean(&input);
        }
        skeletonData.bones.push_back(boneData);
    }

    /* Slots */
    int slotCount = readVarint(&input, true);
    for (int i = 0; i < slotCount; i++) {
        SlotData slotData; 
        slotData.name = readString(&input);
        slotData.bone = skeletonData.bones.at(readVarint(&input, true)).name;
        Color color = readColor(&input);
        if (color != Color{0xff, 0xff, 0xff, 0xff}) slotData.color = color;
        unsigned char reserved = readByte(&input);
        unsigned char r = readByte(&input);
        unsigned char g = readByte(&input);
        unsigned char b = readByte(&input);
        if (!(r == 0xff && g == 0xff && b == 0xff && reserved == 0xff)) {
            slotData.darkColor = Color{r, g, b, 255};
        }
        slotData.attachmentName = readStringRef(&input, &skeletonData);
        slotData.blendMode = static_cast<BlendMode>(readVarint(&input, true));
        if (skeletonData.nonessential) slotData.visible = readBoolean(&input);
        skeletonData.slots.push_back(slotData);
    }

    const auto constraints = readConstraints(&input, skeletonData);

    auto defaultSkin = readSkin(&input, true, &skeletonData, constraints);
    if (!defaultSkin.attachments.empty()) skeletonData.skins.push_back(std::move(defaultSkin));
    for (int i = 0, count = readCount(&input); i < count; ++i)
        skeletonData.skins.push_back(readSkin(&input, false, &skeletonData, constraints));

    for (auto& skin : skeletonData.skins) {
        for (auto& [slotName, attachments] : skin.attachments) {
            for (auto& [name, attachment] : attachments) {
                if (auto* linked = std::get_if<LinkedmeshAttachment>(&attachment.data)) {
                    linked->skin = skeletonData.skins.at(linked->skinIndex).name;
                }
            }
        }
    }

    /* Events */
    int eventCount = readVarint(&input, true);
    for (int i = 0; i < eventCount; i++) {
        EventData eventData; 
        eventData.name = readString(&input).value();
        eventData.intValue = readVarint(&input, false);
        eventData.floatValue = readFloat(&input);
        eventData.stringValue = readString(&input);
        eventData.audioPath = readString(&input);
        if (eventData.audioPath && eventData.audioPath->length() > 0) {
            eventData.volume = readFloat(&input);
            eventData.balance = readFloat(&input);
        }
        skeletonData.events.push_back(eventData);
    }

    /* Animations */
    int animationCount = readVarint(&input, true);
    for (int i = 0; i < animationCount; i++) {
        Animation animation = readAnimation(&input, &skeletonData, constraints);
        skeletonData.animations.push_back(animation);
    }

    for (auto& slider : skeletonData.sliders)
        slider.animation = skeletonData.animations.at(readVarint(&input, true)).name;
    if (input.cursor != input.end) throw std::runtime_error("Unexpected data after Spine 4.3 skeleton");

    return skeletonData;
}

} // namespace

SkeletonData readBinaryData(const Binary& binary) {
    try {
        return readNativeBinaryData(binary);
    } catch (const std::exception& error) {
        try {
            return spine42::readLegacy43BinaryData(binary);
        } catch (const std::exception&) {
            throw std::runtime_error(std::string("Invalid Spine 4.3 binary: ") + error.what());
        }
    }
}

} // namespace spine43
