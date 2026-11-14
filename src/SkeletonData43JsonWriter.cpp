#include "SkeletonData43.h"

namespace spine43 {
namespace {

Json writeSequence(const Sequence& sequence) {
    Json j = Json::object();
    if (sequence.count != 0) j["count"] = sequence.count;
    if (sequence.start != 1) j["start"] = sequence.start;
    if (sequence.digits != 0) j["digits"] = sequence.digits;
    if (sequence.setupIndex != 0) j["setup"] = sequence.setupIndex;
    return j;
}

void writeCurve(const TimelineFrame& frame, Json& j) {
    if (frame.curveType == CurveType::CURVE_STEPPED) {
        j["curve"] = "stepped";
    } else if (frame.curveType == CurveType::CURVE_BEZIER) {
        j["curve"] = frame.curve;
    }
}

void writeTimeline(const std::vector<TimelineFrame>& timeline, Json& j, int valueNum, const std::string& key1, const std::string& key2, float defaultValue) {
    for (const auto& frame : timeline) {
        Json frameJson = Json::object();
        if (frame.time != 0.0f) frameJson["time"] = frame.time;
        if (frame.value1 != defaultValue) frameJson[key1] = frame.value1;
        if (valueNum > 1 && frame.value2 != defaultValue) frameJson[key2] = frame.value2;
        writeCurve(frame, frameJson);
        j.push_back(frameJson);
    }
}

Json writeDrawOrderKeys(const Timeline& keys) {
    Json result = Json::array();
    for (const auto& frame : keys) {
        Json json = Json::object();
        if (frame.time != 0.0f) json["time"] = frame.time;
        for (const auto& [slot, offset] : frame.offsets)
            json["offsets"].push_back({{"slot", slot}, {"offset", offset}});
        result.push_back(std::move(json));
    }
    return result;
}

} // namespace

Json writeJsonData(const SkeletonData& skeletonData) {
    Json j = Json::object();

    Json skeleton = Json::object();
    if (skeletonData.hash != 0) skeleton["hash"] = uint64ToBase64(skeletonData.hash);
    if (skeletonData.version) skeleton["spine"] = skeletonData.version;
    skeleton["x"] = skeletonData.x;
    skeleton["y"] = skeletonData.y;
    skeleton["width"] = skeletonData.width;
    skeleton["height"] = skeletonData.height;
    if (skeletonData.referenceScale != 100.0f) skeleton["referenceScale"] = skeletonData.referenceScale;
    if (skeletonData.nonessential) {
        if (skeletonData.fps != 30.0f) skeleton["fps"] = skeletonData.fps;
        if(skeletonData.imagesPath) skeleton["images"] = skeletonData.imagesPath;
        if(skeletonData.audioPath) skeleton["audio"] = skeletonData.audioPath;
    }
    j["skeleton"] = skeleton;

    /* Bones */
    for (const auto& bone : skeletonData.bones) {
        Json boneJson = Json::object();
        if (bone.name) boneJson["name"] = bone.name;
        if (bone.parent) boneJson["parent"] = bone.parent;
        if (bone.length != 0.0f) boneJson["length"] = bone.length;
        if (bone.x != 0.0f) boneJson["x"] = bone.x;
        if (bone.y != 0.0f) boneJson["y"] = bone.y;
        if (bone.rotation != 0.0f) boneJson["rotation"] = bone.rotation;
        if (bone.scaleX != 1.0f) boneJson["scaleX"] = bone.scaleX;
        if (bone.scaleY != 1.0f) boneJson["scaleY"] = bone.scaleY;
        if (bone.shearX != 0.0f) boneJson["shearX"] = bone.shearX;
        if (bone.shearY != 0.0f) boneJson["shearY"] = bone.shearY;
        if (bone.inherit != Inherit_Normal) boneJson["inherit"] = inheritString.at(bone.inherit);
        if (bone.skinRequired) boneJson["skin"] = bone.skinRequired;
        if (bone.color) boneJson["color"] = colorToString(bone.color.value(), true);
        if (bone.icon && bone.icon != "") boneJson["icon"] = bone.icon;
        if (bone.iconSize != 1.0f) boneJson["iconSize"] = bone.iconSize;
        if (bone.iconRotation != 0.0f) boneJson["iconRotation"] = bone.iconRotation;
        if (!bone.visible) boneJson["visible"] = bone.visible;
        j["bones"].push_back(boneJson);
    }

    /* Slots */
    for (const auto& slot : skeletonData.slots) {
        Json slotJson = Json::object();
        if (slot.name) slotJson["name"] = slot.name;
        if (slot.bone) slotJson["bone"] = slot.bone;
        if (slot.color) slotJson["color"] = colorToString(slot.color.value(), true);
        if (slot.darkColor) slotJson["dark"] = colorToString(slot.darkColor.value(), false);
        if (slot.attachmentName) slotJson["attachment"] = slot.attachmentName;
        if (slot.blendMode != BlendMode_Normal) slotJson["blend"] = blendModeString.at(slot.blendMode);
        if (!slot.visible) slotJson["visible"] = slot.visible;
        j["slots"].push_back(slotJson);
    }

    for (const auto& constraint : orderedConstraints(skeletonData)) {
        Json json = {
            {"name", std::visit([](auto* data) { return data->name; }, constraint)},
            {"type", constraintNames[constraint.index()]}
        };
        std::visit([&](auto* data) {
            if (data->skinRequired) json["skin"] = true;
        }, constraint);
        switch (static_cast<ConstraintType>(constraint.index())) {
            case ConstraintType::Ik: {
                const auto& ik = *std::get<const IKConstraintData*>(constraint);
                json["bones"] = ik.bones;
                json["target"] = ik.target;
                const auto scaleY = scaleYMode(ik);
                if (scaleY != ScaleYMode::None) json["scaleY"] = scaleYNames[static_cast<size_t>(scaleY)];
                if (ik.mix != 1.0f) json["mix"] = ik.mix;
                if (ik.softness != 0.0f) json["softness"] = ik.softness;
                if (!ik.bendPositive) json["bendPositive"] = false;
                if (ik.compress) json["compress"] = true;
                if (ik.stretch) json["stretch"] = true;
                break;
            }
            case ConstraintType::Transform: {
                const auto& transform = *std::get<const TransformConstraintData*>(constraint);
                json["bones"] = transform.bones;
                json["source"] = transform.target;
                const auto mapping = transformMapping(transform);
                if (mapping.localSource) json["localSource"] = true;
                if (mapping.localTarget) json["localTarget"] = true;
                if (mapping.additive) json["additive"] = true;
                if (mapping.clamp) json["clamp"] = true;
                for (const auto& from : mapping.properties) {
                    Json property = Json::object();
                    if (from.offset != 0) property["offset"] = from.offset;
                    for (const auto& to : from.to) {
                        Json target = Json::object();
                        if (to.offset != 0) target["offset"] = to.offset;
                        if (to.max != 1) target["max"] = to.max;
                        if (to.scale != 1) target["scale"] = to.scale;
                        property["to"][propertyNames[static_cast<size_t>(to.property)]] = std::move(target);
                    }
                    json["properties"][propertyNames[static_cast<size_t>(from.property)]] = std::move(property);
                }
                if (transform.offsetRotation != 0) json["rotation"] = transform.offsetRotation;
                if (transform.offsetX != 0) json["x"] = transform.offsetX;
                if (transform.offsetY != 0) json["y"] = transform.offsetY;
                if (transform.offsetScaleX != 0) json["scaleX"] = transform.offsetScaleX;
                if (transform.offsetScaleY != 0) json["scaleY"] = transform.offsetScaleY;
                if (transform.offsetShearY != 0) json["shearY"] = transform.offsetShearY;
                json["mixRotate"] = transform.mixRotate;
                json["mixX"] = transform.mixX;
                json["mixY"] = transform.mixY;
                json["mixScaleX"] = transform.mixScaleX;
                json["mixScaleY"] = transform.mixScaleY;
                json["mixShearY"] = transform.mixShearY;
                break;
            }
            case ConstraintType::Path: {
                const auto& path = *std::get<const PathConstraintData*>(constraint);
                json["bones"] = path.bones;
                json["slot"] = path.target;
                if (path.positionMode != PositionMode_Percent) json["positionMode"] = positionModeString.at(path.positionMode);
                if (path.spacingMode != SpacingMode_Length) json["spacingMode"] = spacingModeString.at(path.spacingMode);
                if (path.rotateMode != RotateMode_Tangent) json["rotateMode"] = rotateModeString.at(path.rotateMode);
                if (path.offsetRotation != 0) json["rotation"] = path.offsetRotation;
                if (path.position != 0) json["position"] = path.position;
                if (path.spacing != 0) json["spacing"] = path.spacing;
                if (path.mixRotate != 1) json["mixRotate"] = path.mixRotate;
                if (path.mixX != 1) json["mixX"] = path.mixX;
                if (path.mixY != path.mixX) json["mixY"] = path.mixY;
                break;
            }
            case ConstraintType::Physics: {
                const auto& physics = *std::get<const PhysicsConstraintData*>(constraint);
                json["bone"] = physics.bone;
                if (physics.x != 0) json["x"] = physics.x;
                if (physics.y != 0) json["y"] = physics.y;
                if (physics.rotate != 0) json["rotate"] = physics.rotate;
                if (physics.scaleX != 0) json["scaleX"] = physics.scaleX;
                if (physics.scaleY != ScaleYMode::None) json["scaleY"] = scaleYNames[static_cast<size_t>(physics.scaleY)];
                if (physics.shearX != 0) json["shearX"] = physics.shearX;
                if (physics.limit != 5000) json["limit"] = physics.limit;
                if (physics.fps != 60) json["fps"] = physics.fps;
                if (physics.inertia != 0.5f) json["inertia"] = physics.inertia;
                if (physics.strength != 100) json["strength"] = physics.strength;
                if (physics.damping != 0.85f) json["damping"] = physics.damping;
                if (physics.mass != 1) json["mass"] = physics.mass;
                if (physics.wind != 0) json["wind"] = physics.wind;
                if (physics.gravity != 0) json["gravity"] = physics.gravity;
                if (physics.mix != 1) json["mix"] = physics.mix;
                if (physics.inertiaGlobal) json["inertiaGlobal"] = true;
                if (physics.strengthGlobal) json["strengthGlobal"] = true;
                if (physics.dampingGlobal) json["dampingGlobal"] = true;
                if (physics.massGlobal) json["massGlobal"] = true;
                if (physics.windGlobal) json["windGlobal"] = true;
                if (physics.gravityGlobal) json["gravityGlobal"] = true;
                if (physics.mixGlobal) json["mixGlobal"] = true;
                break;
            }
            case ConstraintType::Slider: {
                const auto& slider = *std::get<const SliderData*>(constraint);
                json["animation"] = slider.animation;
                if (slider.additive) json["additive"] = true;
                if (slider.loop) json["loop"] = true;
                if (slider.mix != 1) json["mix"] = slider.mix;
                if (slider.bone) {
                    json["bone"] = slider.bone;
                    json["property"] = propertyNames[static_cast<size_t>(slider.property)];
                    if (slider.local) json["local"] = true;
                    if (slider.max != 0) json["max"] = slider.max;
                    if (slider.from != 0) json["from"] = slider.from;
                    if (slider.to != 0) json["to"] = slider.to;
                    if (slider.scale != 1) json["scale"] = slider.scale;
                } else if (slider.time != 0) {
                    json["time"] = slider.time;
                }
                break;
            }
        }
        j["constraints"].push_back(std::move(json));
    }

    /* Skins */
    for (const auto& skin : skeletonData.skins) {
        Json skinJson = Json::object();
        skinJson["name"] = skin.name; 
        if (!skin.bones.empty()) skinJson["bones"] = skin.bones;
        if (!skin.ik.empty()) skinJson["ik"] = skin.ik;
        if (!skin.transform.empty()) skinJson["transform"] = skin.transform;
        if (!skin.path.empty()) skinJson["path"] = skin.path;
        if (!skin.physics.empty()) skinJson["physics"] = skin.physics;
        if (!skin.slider.empty()) skinJson["slider"] = skin.slider;
        if (skin.color) skinJson["color"] = colorToString(*skin.color, true);
        if (!skin.attachments.empty()) {
            for (const auto& [slotName, slotMap] : skin.attachments) {
                for (const auto& [attachmentName, attachmentMap] : slotMap) {
                    Json attachmentJson = Json::object();
                    if (attachmentMap.name != attachmentName) attachmentJson["name"] = attachmentMap.name;
                    if (attachmentMap.path != attachmentMap.name) attachmentJson["path"] = attachmentMap.path;
                    if (attachmentMap.type != AttachmentType::AttachmentType_Region) attachmentJson["type"] = attachmentTypeString.at(attachmentMap.type);
                    switch (attachmentMap.type) {
                        case AttachmentType_Region: {
                            const auto& region = std::get<RegionAttachment>(attachmentMap.data);
                            if (region.x != 0.0f) attachmentJson["x"] = region.x;
                            if (region.y != 0.0f) attachmentJson["y"] = region.y;
                            if (region.rotation != 0.0f) attachmentJson["rotation"] = region.rotation;
                            if (region.scaleX != 1.0f) attachmentJson["scaleX"] = region.scaleX;
                            if (region.scaleY != 1.0f) attachmentJson["scaleY"] = region.scaleY;
                            attachmentJson["width"] = region.width;
                            attachmentJson["height"] = region.height;
                            if (region.color) attachmentJson["color"] = colorToString(region.color.value(), true);
                            if (region.sequence) attachmentJson["sequence"] = writeSequence(region.sequence.value());
                            break;
                        }
                        case AttachmentType_Mesh: {
                            const auto& mesh = std::get<MeshAttachment>(attachmentMap.data);
                            attachmentJson["width"] = mesh.width;
                            attachmentJson["height"] = mesh.height;
                            if (mesh.color) attachmentJson["color"] = colorToString(mesh.color.value(), true);
                            if (mesh.sequence) attachmentJson["sequence"] = writeSequence(mesh.sequence.value());
                            // Older exports can declare a hull inconsistent with their triangle indices.
                            const int hull = binaryMeshHull(mesh).value_or(mesh.hullLength);
                            if (hull != 0) attachmentJson["hull"] = hull;
                            if (!mesh.triangles.empty()) attachmentJson["triangles"] = mesh.triangles;
                            if (!mesh.edges.empty()) attachmentJson["edges"] = mesh.edges;
                            if (!mesh.uvs.empty()) attachmentJson["uvs"] = mesh.uvs;
                            if (!mesh.vertices.empty()) attachmentJson["vertices"] = mesh.vertices;
                            break;
                        }
                        case AttachmentType_Linkedmesh: {
                            const auto& linkedMesh = std::get<LinkedmeshAttachment>(attachmentMap.data);
                            attachmentJson["width"] = linkedMesh.width;
                            attachmentJson["height"] = linkedMesh.height;
                            if (linkedMesh.color) attachmentJson["color"] = colorToString(linkedMesh.color.value(), true);
                            if (linkedMesh.sequence) attachmentJson["sequence"] = writeSequence(linkedMesh.sequence.value());
                            attachmentJson["source"] = linkedMesh.parentMesh;
                            if (linkedMesh.timelines == 0) attachmentJson["timelines"] = false;
                            if (linkedMesh.skin) attachmentJson["skin"] = linkedMesh.skin.value();
                            if (linkedMesh.sourceSlot) attachmentJson["slot"] = linkedMesh.sourceSlot;
                            break; 
                        }
                        case AttachmentType_Boundingbox: {
                            const auto& boundingBox = std::get<BoundingboxAttachment>(attachmentMap.data);
                            if (boundingBox.vertexCount != 0) attachmentJson["vertexCount"] = boundingBox.vertexCount;
                            if (boundingBox.color) attachmentJson["color"] = colorToString(boundingBox.color.value(), true);
                            if (!boundingBox.vertices.empty()) attachmentJson["vertices"] = boundingBox.vertices;
                            break;
                        }
                        case AttachmentType_Path: {
                            const auto& path = std::get<PathAttachment>(attachmentMap.data);
                            if (path.vertexCount != 0) attachmentJson["vertexCount"] = path.vertexCount;
                            if (path.closed) attachmentJson["closed"] = path.closed;
                            if (!path.constantSpeed) attachmentJson["constantSpeed"] = path.constantSpeed;
                            if (path.color) attachmentJson["color"] = colorToString(path.color.value(), true);
                            if (!path.vertices.empty()) attachmentJson["vertices"] = path.vertices;
                            if (!path.lengths.empty()) attachmentJson["lengths"] = path.lengths;
                            break;
                        }
                        case AttachmentType_Point: {
                            const auto& point = std::get<PointAttachment>(attachmentMap.data);
                            if (point.x != 0.0f) attachmentJson["x"] = point.x;
                            if (point.y != 0.0f) attachmentJson["y"] = point.y;
                            if (point.rotation != 0.0f) attachmentJson["rotation"] = point.rotation;
                            if (point.color) attachmentJson["color"] = colorToString(point.color.value(), true);
                            break;
                        }
                        case AttachmentType_Clipping: {
                            const auto& clipping = std::get<ClippingAttachment>(attachmentMap.data);
                            if (clipping.vertexCount != 0) attachmentJson["vertexCount"] = clipping.vertexCount;
                            if (clipping.endSlot) attachmentJson["end"] = clipping.endSlot;
                            if (clipping.convex) attachmentJson["convex"] = true;
                            if (clipping.inverse) attachmentJson["inverse"] = true;
                            if (clipping.color) attachmentJson["color"] = colorToString(clipping.color.value(), true);
                            if (!clipping.vertices.empty()) attachmentJson["vertices"] = clipping.vertices;
                            break;
                        }
                    }
                    skinJson["attachments"][slotName][attachmentName] = attachmentJson; 
                }
            }
        }
        j["skins"].push_back(skinJson);
    }

    /* Events */
    for (const auto& event : skeletonData.events) {
        Json eventJson = Json::object();
        if (event.intValue != 0) eventJson["int"] = event.intValue;
        if (event.floatValue != 0.0f) eventJson["float"] = event.floatValue;
        if (event.stringValue) eventJson["string"] = event.stringValue.value();
        if (event.audioPath) {
            eventJson["audio"] = event.audioPath.value();
            if (event.volume != 1.0f) eventJson["volume"] = event.volume;
            if (event.balance != 0.0f) eventJson["balance"] = event.balance;
        }
        j["events"][event.name] = eventJson;
    }

    /* Animations */
    for (const auto& animation : skeletonData.animations) {
        Json animationJson = Json::object();
        if (animation.color) animationJson["color"] = colorToString(*animation.color, true);
        if (!animation.slots.empty()) {
            for (const auto& [slotName, slotMap] : animation.slots) {
                Json slotJson = Json::object();
                if (slotMap.contains("attachment")) {
                    for (const auto& frame : slotMap.at("attachment")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.str1) frameJson["name"] = frame.str1.value();
                        slotJson["attachment"].push_back(frameJson);
                    }
                }
                if (slotMap.contains("rgba")) {
                    for (const auto& frame : slotMap.at("rgba")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.color1) frameJson["color"] = colorToString(frame.color1.value(), true);
                        writeCurve(frame, frameJson);
                        slotJson["rgba"].push_back(frameJson);
                    }
                }
                if (slotMap.contains("rgb")) {
                    for (const auto& frame : slotMap.at("rgb")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.color1) frameJson["color"] = colorToString(frame.color1.value(), false);
                        writeCurve(frame, frameJson);
                        slotJson["rgb"].push_back(frameJson);
                    }
                }
                if (slotMap.contains("alpha")) {
                    writeTimeline(slotMap.at("alpha"), slotJson["alpha"], 1, "value", "", 0.0f);
                }
                if (slotMap.contains("rgba2")) {
                    for (const auto& frame : slotMap.at("rgba2")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.color1) frameJson["light"] = colorToString(frame.color1.value(), true);
                        if (frame.color2) frameJson["dark"] = colorToString(frame.color2.value(), false);
                        writeCurve(frame, frameJson);
                        slotJson["rgba2"].push_back(frameJson);
                    }
                }
                if (slotMap.contains("rgb2")) {
                    for (const auto& frame : slotMap.at("rgb2")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.color1) frameJson["light"] = colorToString(frame.color1.value(), false);
                        if (frame.color2) frameJson["dark"] = colorToString(frame.color2.value(), false);
                        writeCurve(frame, frameJson);
                        slotJson["rgb2"].push_back(frameJson);
                    }
                }
                animationJson["slots"][slotName] = slotJson;
            }
        }
        if (!animation.bones.empty()) {
            for (const auto& [boneName, boneMap] : animation.bones) {
                Json boneJson = Json::object();
                if (boneMap.contains("rotate")) {
                    writeTimeline(boneMap.at("rotate"), boneJson["rotate"], 1, "value", "", 0.0f);
                }
                if (boneMap.contains("translate")) {
                    writeTimeline(boneMap.at("translate"), boneJson["translate"], 2, "x", "y", 0.0f);
                }
                if (boneMap.contains("translatex")) {
                    writeTimeline(boneMap.at("translatex"), boneJson["translatex"], 1, "value", "", 0.0f);
                }
                if (boneMap.contains("translatey")) {
                    writeTimeline(boneMap.at("translatey"), boneJson["translatey"], 1, "value", "", 0.0f);
                }
                if (boneMap.contains("scale")) {
                    writeTimeline(boneMap.at("scale"), boneJson["scale"], 2, "x", "y", 1.0f);
                }
                if (boneMap.contains("scalex")) {
                    writeTimeline(boneMap.at("scalex"), boneJson["scalex"], 1, "value", "", 1.0f);
                }
                if (boneMap.contains("scaley")) {
                    writeTimeline(boneMap.at("scaley"), boneJson["scaley"], 1, "value", "", 1.0f);
                }
                if (boneMap.contains("shear")) {
                    writeTimeline(boneMap.at("shear"), boneJson["shear"], 2, "x", "y", 0.0f);
                }
                if (boneMap.contains("shearx")) {
                    writeTimeline(boneMap.at("shearx"), boneJson["shearx"], 1, "value", "", 0.0f);
                }
                if (boneMap.contains("sheary")) {
                    writeTimeline(boneMap.at("sheary"), boneJson["sheary"], 1, "value", "", 0.0f);
                }
                if (boneMap.contains("inherit")) {
                    for (const auto& frame : boneMap.at("inherit")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.inherit != Inherit_Normal) frameJson["inherit"] = inheritString.at(frame.inherit);
                        boneJson["inherit"].push_back(frameJson);
                    }
                }
                animationJson["bones"][boneName] = boneJson;
            }
        }
        if (!animation.ik.empty()) {
            for (const auto& [ikName, ikTimeline] : animation.ik) {
                Json ikJson = Json::array();
                for (const auto& frame : ikTimeline) {
                    Json frameJson = Json::object();
                    if (frame.time != 0.0f) frameJson["time"] = frame.time;
                    if (frame.value1 != 1.0f) frameJson["mix"] = frame.value1;
                    if (frame.value2 != 0.0f) frameJson["softness"] = frame.value2;
                    if (!frame.bendPositive) frameJson["bendPositive"] = frame.bendPositive;
                    if (frame.compress) frameJson["compress"] = frame.compress;
                    if (frame.stretch) frameJson["stretch"] = frame.stretch;
                    writeCurve(frame, frameJson);
                    ikJson.push_back(frameJson);
                }
                animationJson["ik"][ikName] = ikJson;
            }
        }
        if (!animation.transform.empty()) {
            for (const auto& [transformName, transformTimeline] : animation.transform) {
                Json transformJson = Json::array();
                for (const auto& frame : transformTimeline) {
                    Json frameJson = Json::object();
                    if (frame.time != 0.0f) frameJson["time"] = frame.time;
                    if (frame.value1 != 1.0f) frameJson["mixRotate"] = frame.value1;
                    if (frame.value2 != 1.0f) frameJson["mixX"] = frame.value2;
                    if (frame.value3 != frame.value2) frameJson["mixY"] = frame.value3;
                    if (frame.value4 != 1.0f) frameJson["mixScaleX"] = frame.value4;
                    // 4.3 readers have used both 1 and mixScaleX as the default.
                    if (frame.value5 != 1.0f || frame.value4 != 1.0f) frameJson["mixScaleY"] = frame.value5;
                    if (frame.value6 != 1.0f) frameJson["mixShearY"] = frame.value6;
                    writeCurve(frame, frameJson);
                    transformJson.push_back(frameJson);
                }
                animationJson["transform"][transformName] = transformJson;
            }
        }
        if (!animation.path.empty()) {
            for (const auto& [pathName, pathMap] : animation.path) {
                Json pathJson = Json::object();
                if (pathMap.contains("position")) {
                    writeTimeline(pathMap.at("position"), pathJson["position"], 1, "value", "", 0.0f);
                }
                if (pathMap.contains("spacing")) {
                    writeTimeline(pathMap.at("spacing"), pathJson["spacing"], 1, "value", "", 0.0f);
                }
                if (pathMap.contains("mix")) {
                    for (const auto& frame : pathMap.at("mix")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        if (frame.value1 != 1.0f) frameJson["mixRotate"] = frame.value1;
                        if (frame.value2 != 1.0f) frameJson["mixX"] = frame.value2;
                        if (frame.value3 != frame.value2) frameJson["mixY"] = frame.value3; 
                        writeCurve(frame, frameJson);
                        pathJson["mix"].push_back(frameJson);
                    }
                }
                animationJson["path"][pathName] = pathJson;
            }
        }
        if (!animation.physics.empty()) {
            for (const auto& [physicsName, physicsMap] : animation.physics) {
                Json physicsJson = Json::object();
                if (physicsMap.contains("reset")) {
                    for (const auto& frame : physicsMap.at("reset")) {
                        Json frameJson = Json::object();
                        if (frame.time != 0.0f) frameJson["time"] = frame.time;
                        physicsJson["reset"].push_back(frameJson);
                    }
                }
                if (physicsMap.contains("inertia")) {
                    writeTimeline(physicsMap.at("inertia"), physicsJson["inertia"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("strength")) {
                    writeTimeline(physicsMap.at("strength"), physicsJson["strength"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("damping")) {
                    writeTimeline(physicsMap.at("damping"), physicsJson["damping"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("mass")) {
                    writeTimeline(physicsMap.at("mass"), physicsJson["mass"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("wind")) {
                    writeTimeline(physicsMap.at("wind"), physicsJson["wind"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("gravity")) {
                    writeTimeline(physicsMap.at("gravity"), physicsJson["gravity"], 1, "value", "", 0.0f);
                }
                if (physicsMap.contains("mix")) {
                    writeTimeline(physicsMap.at("mix"), physicsJson["mix"], 1, "value", "", 1.0f);
                }
                animationJson["physics"][physicsName] = physicsJson;
            }
        }
        for (const auto& [name, timelines] : animation.slider) {
            for (const auto& [type, keys] : timelines)
                writeTimeline(keys, animationJson["slider"][name][type], 1, "value", "", 1.0f);
        }
        if (!animation.attachments.empty()) {
            for (const auto& [skinName, skinMap] : animation.attachments) {
                for (const auto& [slotName, slotMap] : skinMap) {
                    for (const auto& [attachmentName, attachmentMap] : slotMap) {
                        Json attachmentJson = Json::object();
                        if (attachmentMap.contains("deform")) {
                            for (const auto& frame : attachmentMap.at("deform")) {
                                Json frameJson = Json::object();
                                if (frame.time != 0.0f) frameJson["time"] = frame.time;
                                if (!frame.vertices.empty()) {
                                    if (frame.int1 != 0) frameJson["offset"] = frame.int1; 
                                    frameJson["vertices"] = frame.vertices; 
                                }
                                writeCurve(frame, frameJson); 
                                attachmentJson["deform"].push_back(frameJson);
                            }
                        }
                        if (attachmentMap.contains("sequence")) {
                            float lastDelay = 0.0f; 
                            for (const auto& frame : attachmentMap.at("sequence")) {
                                Json frameJson = Json::object();
                                if (frame.time != 0.0f) frameJson["time"] = frame.time;
                                if (frame.value1 != lastDelay) frameJson["delay"] = frame.value1; 
                                lastDelay = frame.value1;
                                if (frame.int1 != 0) frameJson["index"] = frame.int1;
                                if (frame.sequenceMode != SequenceMode::hold) frameJson["mode"] = sequenceModeString.at(frame.sequenceMode);
                                attachmentJson["sequence"].push_back(frameJson);
                            }
                        }
                        animationJson["attachments"][skinName][slotName][attachmentName] = attachmentJson;
                    }
                }
            }
        }
        if (!animation.drawOrder.empty()) {
            animationJson["drawOrder"] = writeDrawOrderKeys(animation.drawOrder);
        }
        for (const auto& folder : animation.drawOrderFolders) {
            animationJson["drawOrderFolder"].push_back({
                {"slots", folder.slots}, {"keys", writeDrawOrderKeys(folder.keys)}
            });
        }
        if (!animation.events.empty()) {
            for (const auto& frame : animation.events) {
                Json frameJson = Json::object(); 
                if (frame.time != 0.0f) frameJson["time"] = frame.time; 
                if (frame.str1) frameJson["name"] = frame.str1; 
                int eventIndex = -1; 
                for (size_t i = 0; i < skeletonData.events.size(); i++) {
                    if (skeletonData.events[i].name == frame.str1) {
                        eventIndex = static_cast<int>(i);
                        break;
                    }
                }
                EventData eventData = skeletonData.events[eventIndex];
                if (frame.int1 != eventData.intValue) frameJson["int"] = frame.int1;
                if (frame.value1 != eventData.floatValue) frameJson["float"] = frame.value1;
                if (frame.str2) frameJson["string"] = frame.str2;
                if (eventData.audioPath && !eventData.audioPath->empty()) {
                    if (frame.value2 != eventData.volume) frameJson["volume"] = frame.value2;
                    if (frame.value3 != eventData.balance) frameJson["balance"] = frame.value3;
                }
                animationJson["events"].push_back(frameJson);
            }
        }
        j["animations"][animation.name] = animationJson;
    }

    return j;
}

}
