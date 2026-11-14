#include "SkeletonData43.h"

namespace spine43 {
namespace {

OptStr optionalString(const Json& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end() || it->is_null()) return std::nullopt;
    return it->get<std::string>();
}

TimelineFrame readDrawOrderKey(const Json& key) {
    TimelineFrame frame;
    frame.time = key.value("time", 0.0f);
    if (key.contains("offsets")) {
        for (const auto& offset : key["offsets"]) {
            frame.offsets.emplace_back(offset.at("slot").get<std::string>(), offset.value("offset", 0));
        }
    }
    return frame;
}

Sequence readSequence(const Json& j) {
    Sequence sequence;
    sequence.count = j.value("count", 0);
    sequence.start = j.value("start", 1);
    sequence.digits = j.value("digits", 0);
    sequence.setupIndex = j.value("setup", j.value("setupIndex", 0));
    return sequence;
}

void readCurve(const Json& j, TimelineFrame& frame) {
    if (j.contains("curve")) {
        if (j["curve"] == "stepped") {
            frame.curveType = CurveType::CURVE_STEPPED;
        } else {
            frame.curveType = CURVE_BEZIER; 
            frame.curve = j["curve"].get<std::vector<float>>();
        }
    }
}

void readTimeline(const Json& j, Timeline& timeline, int valueNum, const std::string& key1, const std::string& key2, float defaultValue) {
    for (const auto& frameJson : j) {
        TimelineFrame frame;
        frame.time = frameJson.value("time", 0.0f);
        frame.value1 = frameJson.value(key1, defaultValue);
        if (valueNum > 1) frame.value2 = frameJson.value(key2, defaultValue);
        readCurve(frameJson, frame);
        timeline.push_back(frame);
    }
}

} // namespace

SkeletonData readJsonData(const Json& j) {
    SkeletonData skeletonData; 

    const auto& skeleton = j["skeleton"];
    skeletonData.hash = base64ToUint64(optionalString(skeleton, "hash").value_or(""));
    if (skeleton.contains("spine")) skeletonData.version = skeleton["spine"];
    skeletonData.x = skeleton.value("x", 0.0f);
    skeletonData.y = skeleton.value("y", 0.0f);
    skeletonData.width = skeleton.value("width", 0.0f);
    skeletonData.height = skeleton.value("height", 0.0f);
    skeletonData.referenceScale = skeleton.value("referenceScale", 100.0f);
    skeletonData.fps = skeleton.value("fps", 30.0f);
    skeletonData.imagesPath = optionalString(skeleton, "images");
    skeletonData.audioPath = optionalString(skeleton, "audio");
    skeletonData.nonessential = !(skeletonData.fps == 30.0f && skeletonData.imagesPath == std::nullopt && skeletonData.audioPath == std::nullopt);

    /* Bones */
    if (j.contains("bones")) {
        for (const auto& boneJson : j["bones"]) {
            BoneData boneData;
            if (boneJson.contains("name")) boneData.name = boneJson["name"];
            if (boneJson.contains("parent")) boneData.parent = boneJson["parent"];
            boneData.length = boneJson.value("length", 0.0f);
            boneData.x = boneJson.value("x", 0.0f);
            boneData.y = boneJson.value("y", 0.0f);
            boneData.rotation = boneJson.value("rotation", 0.0f);
            boneData.scaleX = boneJson.value("scaleX", 1.0f);
            boneData.scaleY = boneJson.value("scaleY", 1.0f);
            boneData.shearX = boneJson.value("shearX", 0.0f);
            boneData.shearY = boneJson.value("shearY", 0.0f);
            boneData.inherit = inheritMap.at(boneJson.value("inherit", "normal"));
            boneData.skinRequired = boneJson.value("skin", false);
            if (boneJson.contains("color")) boneData.color = stringToColor(boneJson["color"], true);
            boneData.icon = boneJson.value("icon", "");
            boneData.iconSize = boneJson.value("iconSize", 1.0f);
            boneData.iconRotation = boneJson.value("iconRotation", 0.0f);
            boneData.visible = boneJson.value("visible", true);
            skeletonData.nonessential |= boneData.color.has_value() || !boneData.icon->empty() ||
                boneData.iconSize != 1.0f || boneData.iconRotation != 0.0f || !boneData.visible;
            skeletonData.bones.push_back(boneData);
        }
    }

    /* Slots */
    if (j.contains("slots")) {
        for (const auto& slotJson : j["slots"]) {
            SlotData slotData;
            if (slotJson.contains("name")) slotData.name = slotJson["name"];
            if (slotJson.contains("bone")) slotData.bone = slotJson["bone"];
            if (slotJson.contains("color")) slotData.color = stringToColor(slotJson["color"], true);
            if (slotJson.contains("dark")) slotData.darkColor = stringToColor(slotJson["dark"], false);
            if (slotJson.contains("attachment") && !slotJson["attachment"].is_null()) slotData.attachmentName = slotJson["attachment"];
            slotData.blendMode = blendModeMap.at(slotJson.value("blend", "normal"));
            slotData.visible = slotJson.value("visible", true);
            skeletonData.nonessential |= !slotData.visible;
            skeletonData.slots.push_back(slotData);
        }
    }

    // Accept the grouped constraints emitted by older versions of this converter.
    Json grouped;
    const bool native = j.contains("constraints");
    if (native) {
        size_t order = 0;
        for (auto constraint : j.at("constraints")) {
            const auto type = enumValue<ConstraintType>(constraintNames, constraint.at("type").get<std::string>());
            constraint["order"] = order++;
            grouped[constraintNames[static_cast<size_t>(type)]].push_back(std::move(constraint));
        }
    }
    const auto& constraints = native ? grouped : j;

    /* IK constraints */
    if (constraints.contains("ik")) {
        for (const auto& ikJson : constraints["ik"]) {
            IKConstraintData ikData;
            if (ikJson.contains("name")) ikData.name = ikJson["name"];
            ikData.order = ikJson.value("order", 0);
            ikData.skinRequired = ikJson.value("skin", false);
            if (ikJson.contains("bones")) ikData.bones = ikJson["bones"].get<std::vector<std::string>>();
            if (ikJson.contains("target")) ikData.target = ikJson["target"];
            ikData.mix = ikJson.value("mix", 1.0f);
            ikData.softness = ikJson.value("softness", 0.0f);
            ikData.bendPositive = ikJson.value("bendPositive", true);
            ikData.compress = ikJson.value("compress", false);
            ikData.stretch = ikJson.value("stretch", false);
            ikData.uniform = ikJson.value("uniform", false);
            ikData.scaleY = enumValue<ScaleYMode>(scaleYNames, ikJson.value("scaleY", "none"));
            skeletonData.ikConstraints.push_back(ikData);
        }
    }

    /* Transform constraints */
    if (constraints.contains("transform")) {
        for (const auto& transformJson : constraints["transform"]) {
            TransformConstraintData transformData;
            if (transformJson.contains("name")) transformData.name = transformJson["name"];
            transformData.order = transformJson.value("order", 0);
            transformData.skinRequired = transformJson.value("skin", false);
            if (transformJson.contains("bones")) transformData.bones = transformJson["bones"].get<std::vector<std::string>>();
            transformData.target = optionalString(transformJson, native ? "source" : "target");
            transformData.mixRotate = transformJson.value("mixRotate", 1.0f);
            transformData.mixX = transformJson.value("mixX", 1.0f);
            transformData.mixY = transformJson.value("mixY", transformData.mixX);
            transformData.mixScaleX = transformJson.value("mixScaleX", 1.0f);
            transformData.mixScaleY = transformJson.value("mixScaleY", transformData.mixScaleX);
            transformData.mixShearY = transformJson.value("mixShearY", 1.0f);
            transformData.offsetRotation = transformJson.value("rotation", 0.0f);
            transformData.offsetX = transformJson.value("x", 0.0f);
            transformData.offsetY = transformJson.value("y", 0.0f);
            transformData.offsetScaleX = transformJson.value("scaleX", 0.0f);
            transformData.offsetScaleY = transformJson.value("scaleY", 0.0f);
            transformData.offsetShearY = transformJson.value("shearY", 0.0f);
            transformData.relative = transformJson.value("relative", false);
            transformData.local = transformJson.value("local", false);
            if (native) {
                auto& mapping = transformData.mapping.emplace();
                mapping.localSource = transformJson.value("localSource", false);
                mapping.localTarget = transformJson.value("localTarget", false);
                mapping.additive = transformJson.value("additive", false);
                mapping.clamp = transformJson.value("clamp", false);
                transformData.local = mapping.localSource && mapping.localTarget;
                transformData.relative = mapping.additive;
                std::array<bool, 6> used{};
                if (transformJson.contains("properties")) {
                    for (const auto& [property, json] : transformJson["properties"].items()) {
                        TransformFromProperty from{enumValue<TransformProperty>(propertyNames, property), json.value("offset", 0.0f), {}};
                        if (json.contains("to")) {
                            for (const auto& [target, values] : json["to"].items()) {
                                const auto to = enumValue<TransformProperty>(propertyNames, target);
                                used[static_cast<size_t>(to)] = true;
                                from.to.push_back({to, values.value("offset", 0.0f), values.value("max", 1.0f), values.value("scale", 1.0f)});
                            }
                        }
                        if (!from.to.empty()) mapping.properties.push_back(std::move(from));
                    }
                }
                if (!used[0]) transformData.mixRotate = 0;
                if (!used[1]) transformData.mixX = 0;
                transformData.mixY = used[2] ? transformJson.value("mixY", transformData.mixX) : 0;
                if (!used[3]) transformData.mixScaleX = 0;
                transformData.mixScaleY = used[4] ? transformJson.value("mixScaleY", transformData.mixScaleX) : 0;
                if (!used[5]) transformData.mixShearY = 0;
            }
            skeletonData.transformConstraints.push_back(transformData);
        }
    }

    /* Path constraints */
    if (constraints.contains("path")) {
        for (const auto& pathJson : constraints["path"]) {
            PathConstraintData pathData;
            if (pathJson.contains("name")) pathData.name = pathJson["name"];
            pathData.order = pathJson.value("order", 0);
            pathData.skinRequired = pathJson.value("skin", false);
            if (pathJson.contains("bones")) pathData.bones = pathJson["bones"].get<std::vector<std::string>>();
            pathData.target = optionalString(pathJson, native ? "slot" : "target");
            pathData.positionMode = positionModeMap.at(pathJson.value("positionMode", "percent"));
            pathData.spacingMode = spacingModeMap.at(pathJson.value("spacingMode", "length"));
            pathData.rotateMode = rotateModeMap.at(pathJson.value("rotateMode", "tangent"));
            pathData.offsetRotation = pathJson.value("rotation", 0.0f);
            pathData.position = pathJson.value("position", 0.0f);
            pathData.spacing = pathJson.value("spacing", 0.0f);
            pathData.mixRotate = pathJson.value("mixRotate", 1.0f);
            pathData.mixX = pathJson.value("mixX", 1.0f);
            pathData.mixY = pathJson.value("mixY", pathData.mixX);
            skeletonData.pathConstraints.push_back(pathData);
        }
    }

    /* Physics constraints */
    if (constraints.contains("physics")) {
        for (const auto& physicsJson : constraints["physics"]) {
            PhysicsConstraintData physicsData;
            if (physicsJson.contains("name")) physicsData.name = physicsJson["name"];
            physicsData.order = physicsJson.value("order", 0);
            physicsData.skinRequired = physicsJson.value("skin", false);
            if (physicsJson.contains("bone")) physicsData.bone = physicsJson["bone"];
            physicsData.x = physicsJson.value("x", 0.0f);
            physicsData.y = physicsJson.value("y", 0.0f);
            physicsData.rotate = physicsJson.value("rotate", 0.0f);
            physicsData.scaleX = physicsJson.value("scaleX", 0.0f);
            physicsData.scaleY = enumValue<ScaleYMode>(scaleYNames, physicsJson.value("scaleY", "none"));
            physicsData.shearX = physicsJson.value("shearX", 0.0f);
            physicsData.limit = physicsJson.value("limit", 5000.0f);
            physicsData.fps = physicsJson.value("fps", 60.0f);
            physicsData.inertia = physicsJson.value("inertia", native ? 0.5f : 1.0f);
            physicsData.strength = physicsJson.value("strength", 100.0f);
            physicsData.damping = physicsJson.value("damping", native ? 0.85f : 1.0f);
            physicsData.mass = physicsJson.value("mass", 1.0f);
            physicsData.wind = physicsJson.value("wind", 0.0f);
            physicsData.gravity = physicsJson.value("gravity", 0.0f);
            physicsData.mix = physicsJson.value("mix", 1.0f);
            physicsData.inertiaGlobal = physicsJson.value("inertiaGlobal", false);
            physicsData.strengthGlobal = physicsJson.value("strengthGlobal", false);
            physicsData.dampingGlobal = physicsJson.value("dampingGlobal", false);
            physicsData.massGlobal = physicsJson.value("massGlobal", false);
            physicsData.windGlobal = physicsJson.value("windGlobal", false);
            physicsData.gravityGlobal = physicsJson.value("gravityGlobal", false);
            physicsData.mixGlobal = physicsJson.value("mixGlobal", false);
            skeletonData.physicsConstraints.push_back(physicsData);
        }
    }

    if (constraints.contains("slider")) {
        for (const auto& json : constraints["slider"]) {
            SliderData slider;
            slider.name = json.at("name");
            slider.order = json.value("order", 0);
            slider.animation = optionalString(json, "animation");
            slider.skinRequired = json.value("skin", false);
            slider.loop = json.value("loop", false);
            slider.additive = json.value("additive", false);
            slider.mix = json.value("mix", 1.0f);
            slider.bone = optionalString(json, "bone");
            if (slider.bone) {
                slider.property = enumValue<TransformProperty>(propertyNames, json.at("property").get<std::string>());
                slider.local = json.value("local", false);
                slider.max = json.value("max", 0.0f);
                slider.from = json.value("from", 0.0f);
                slider.to = json.value("to", 0.0f);
                slider.scale = json.value("scale", 1.0f);
                skeletonData.nonessential |= slider.max != 0.0f;
            } else {
                slider.time = json.value("time", 0.0f);
            }
            skeletonData.sliders.push_back(slider);
        }
    }

    /* Skins */
    if (j.contains("skins")) {
        for (const auto& skinJson : j["skins"]) {
            Skin skinData;
            skinData.name = skinJson.value("name", "");
            if (skinJson.contains("bones")) skinData.bones = skinJson["bones"].get<std::vector<std::string>>();
            if (skinJson.contains("ik")) skinData.ik = skinJson["ik"].get<std::vector<std::string>>();
            if (skinJson.contains("transform")) skinData.transform = skinJson["transform"].get<std::vector<std::string>>();
            if (skinJson.contains("path")) skinData.path = skinJson["path"].get<std::vector<std::string>>();
            if (skinJson.contains("physics")) skinData.physics = skinJson["physics"].get<std::vector<std::string>>();
            if (skinJson.contains("slider")) skinData.slider = skinJson["slider"].get<std::vector<std::string>>();
            if (skinJson.contains("color")) {
                skinData.color = stringToColor(skinJson["color"], true);
                skeletonData.nonessential = true;
            }
            if (skinJson.contains("attachments")) {
                for (const auto& [slotName, slotAttachments] : skinJson["attachments"].items()) {
                    skinData.attachments[slotName] = {};
                    for (const auto& [attachmentName, attachmentJson] : slotAttachments.items()) {
                        Attachment attachment;
                        attachment.name = attachmentJson.value("name", attachmentName);
                        attachment.path = attachmentJson.value("path", attachment.name);
                        attachment.type = attachmentTypeMap.at(attachmentJson.value("type", "region"));
                        if (attachment.type == AttachmentType_Mesh && attachmentJson.contains("source")) {
                            attachment.type = AttachmentType_Linkedmesh;
                        }
                        skeletonData.nonessential |= attachmentJson.contains("edges") ||
                            ((attachment.type != AttachmentType_Region) && (attachmentJson.contains("width") || attachmentJson.contains("height"))) ||
                            ((attachment.type != AttachmentType_Region && attachment.type != AttachmentType_Mesh &&
                              attachment.type != AttachmentType_Linkedmesh) && attachmentJson.contains("color"));
                        switch (attachment.type) {
                            case AttachmentType_Region: {
                                RegionAttachment region;
                                region.x = attachmentJson.value("x", 0.0f);
                                region.y = attachmentJson.value("y", 0.0f);
                                region.rotation = attachmentJson.value("rotation", 0.0f);
                                region.scaleX = attachmentJson.value("scaleX", 1.0f);
                                region.scaleY = attachmentJson.value("scaleY", 1.0f);
                                region.width = attachmentJson.value("width", 0.0f);
                                region.height = attachmentJson.value("height", 0.0f);
                                if (attachmentJson.contains("color")) region.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("sequence") && !attachmentJson["sequence"].is_null()) region.sequence = readSequence(attachmentJson["sequence"]);
                                attachment.data = region;
                                break; 
                            }
                            case AttachmentType_Mesh: {
                                MeshAttachment mesh;
                                mesh.width = attachmentJson.value("width", 0.0f);
                                mesh.height = attachmentJson.value("height", 0.0f);
                                if (attachmentJson.contains("color")) mesh.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("sequence") && !attachmentJson["sequence"].is_null()) mesh.sequence = readSequence(attachmentJson["sequence"]);
                                mesh.hullLength = attachmentJson.value("hull", 0);
                                mesh.triangles = attachmentJson.value("triangles", std::vector<unsigned short>{});
                                mesh.edges = attachmentJson.value("edges", std::vector<unsigned short>{});
                                mesh.uvs = attachmentJson.value("uvs", std::vector<float>{});
                                mesh.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = mesh;
                                break; 
                            }
                            case AttachmentType_Linkedmesh: {
                                LinkedmeshAttachment linkedMesh;
                                linkedMesh.width = attachmentJson.value("width", 0.0f);
                                linkedMesh.height = attachmentJson.value("height", 0.0f);
                                if (attachmentJson.contains("color")) linkedMesh.color = stringToColor(attachmentJson["color"], true);
                                if (attachmentJson.contains("sequence") && !attachmentJson["sequence"].is_null()) linkedMesh.sequence = readSequence(attachmentJson["sequence"]);
                                linkedMesh.parentMesh = attachmentJson.at(attachmentJson.contains("source") ? "source" : "parent");
                                if (attachmentJson.contains("timelines") && attachmentJson["timelines"].is_number_integer())
                                    linkedMesh.timelines = attachmentJson["timelines"].get<int>();
                                else linkedMesh.timelines = attachmentJson.value("timelines", true);
                                linkedMesh.skin = optionalString(attachmentJson, "skin");
                                linkedMesh.sourceSlot = optionalString(attachmentJson, "slot");
                                attachment.data = linkedMesh;
                                break; 
                            }
                            case AttachmentType_Boundingbox: {
                                BoundingboxAttachment boundingBox;
                                boundingBox.vertexCount = attachmentJson.value("vertexCount", 0);
                                if (attachmentJson.contains("color")) boundingBox.color = stringToColor(attachmentJson["color"], true);
                                boundingBox.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = boundingBox;
                                break; 
                            }
                            case AttachmentType_Path: {
                                PathAttachment path;
                                path.vertexCount = attachmentJson.value("vertexCount", 0);
                                path.closed = attachmentJson.value("closed", false);
                                path.constantSpeed = attachmentJson.value("constantSpeed", true);
                                if (attachmentJson.contains("color")) path.color = stringToColor(attachmentJson["color"], true);
                                path.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                path.lengths = attachmentJson.value("lengths", std::vector<float>{});
                                attachment.data = path;
                                break; 
                            }
                            case AttachmentType_Point: {
                                PointAttachment point;
                                point.x = attachmentJson.value("x", 0.0f);
                                point.y = attachmentJson.value("y", 0.0f);
                                point.rotation = attachmentJson.value("rotation", 0.0f);
                                if (attachmentJson.contains("color")) point.color = stringToColor(attachmentJson["color"], true);
                                attachment.data = point;
                                break; 
                            }
                            case AttachmentType_Clipping: {
                                ClippingAttachment clipping;
                                clipping.convex = attachmentJson.value("convex", false);
                                clipping.inverse = attachmentJson.value("inverse", false);
                                clipping.vertexCount = attachmentJson.value("vertexCount", 0);
                                if (attachmentJson.contains("color")) clipping.color = stringToColor(attachmentJson["color"], true);
                                clipping.endSlot = optionalString(attachmentJson, "end");
                                clipping.vertices = attachmentJson.value("vertices", std::vector<float>{});
                                attachment.data = clipping;
                                break; 
                            }
                        }
                        skinData.attachments[slotName][attachmentName] = attachment;
                    }
                }
            }
            skeletonData.skins.push_back(skinData);
        }
    }

    /* Events */
    if (j.contains("events")) {
        for (const auto& [eventName, eventJson] : j["events"].items()) {
            EventData eventData;
            eventData.name = eventName;
            eventData.intValue = eventJson.value("int", 0);
            eventData.floatValue = eventJson.value("float", 0.0f);
            eventData.stringValue = optionalString(eventJson, "string");
            eventData.audioPath = optionalString(eventJson, "audio");
            if (eventData.audioPath && !eventData.audioPath->empty()) {
                eventData.volume = eventJson.value("volume", 1.0f);
                eventData.balance = eventJson.value("balance", 0.0f);
            }
            skeletonData.events.push_back(eventData);
        }
    }

    /* Animations */
    if (j.contains("animations")) {
        for (const auto& [animationName, animationJson] : j["animations"].items()) {
            Animation animationData;
            animationData.name = animationName;
            if (animationJson.contains("slots")) {
                for (const auto& [slotName, slotJson] : animationJson["slots"].items()) {
                    MultiTimeline slotTimeline;
                    if (slotJson.contains("attachment")) {
                        for (const auto& frameJson : slotJson["attachment"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("name") && !frameJson["name"].is_null()) frame.str1 = frameJson["name"];
                            slotTimeline["attachment"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgba")) {
                        for (const auto& frameJson : slotJson["rgba"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("color")) frame.color1 = stringToColor(frameJson["color"], true);
                            readCurve(frameJson, frame);
                            slotTimeline["rgba"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgb")) {
                        for (const auto& frameJson : slotJson["rgb"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("color")) frame.color1 = stringToColor(frameJson["color"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgb"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("alpha")) {
                        readTimeline(slotJson["alpha"], slotTimeline["alpha"], 1, "value", "", 0.0f);
                    }
                    if (slotJson.contains("rgba2")) {
                        for (const auto& frameJson : slotJson["rgba2"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("light")) frame.color1 = stringToColor(frameJson["light"], true);
                            if (frameJson.contains("dark")) frame.color2 = stringToColor(frameJson["dark"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgba2"].push_back(frame);
                        }
                    }
                    if (slotJson.contains("rgb2")) {
                        for (const auto& frameJson : slotJson["rgb2"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            if (frameJson.contains("light")) frame.color1 = stringToColor(frameJson["light"], false);
                            if (frameJson.contains("dark")) frame.color2 = stringToColor(frameJson["dark"], false);
                            readCurve(frameJson, frame);
                            slotTimeline["rgb2"].push_back(frame);
                        }
                    }
                    animationData.slots[slotName] = slotTimeline;
                }
            }
            if (animationJson.contains("bones")) {
                for (const auto& [boneName, boneJson] : animationJson["bones"].items()) {
                    MultiTimeline boneTimeline;
                    if (boneJson.contains("rotate")) {
                        readTimeline(boneJson["rotate"], boneTimeline["rotate"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("translate")) {
                        readTimeline(boneJson["translate"], boneTimeline["translate"], 2, "x", "y", 0.0f);
                    }
                    if (boneJson.contains("translatex")) {
                        readTimeline(boneJson["translatex"], boneTimeline["translatex"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("translatey")) {
                        readTimeline(boneJson["translatey"], boneTimeline["translatey"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("scale")) {
                        readTimeline(boneJson["scale"], boneTimeline["scale"], 2, "x", "y", 1.0f);
                    }
                    if (boneJson.contains("scalex")) {
                        readTimeline(boneJson["scalex"], boneTimeline["scalex"], 1, "value", "", 1.0f);
                    }
                    if (boneJson.contains("scaley")) {
                        readTimeline(boneJson["scaley"], boneTimeline["scaley"], 1, "value", "", 1.0f);
                    }
                    if (boneJson.contains("shear")) {
                        readTimeline(boneJson["shear"], boneTimeline["shear"], 2, "x", "y", 0.0f);
                    }
                    if (boneJson.contains("shearx")) {
                        readTimeline(boneJson["shearx"], boneTimeline["shearx"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("sheary")) {
                        readTimeline(boneJson["sheary"], boneTimeline["sheary"], 1, "value", "", 0.0f);
                    }
                    if (boneJson.contains("inherit")) {
                        for (const auto& frameJson : boneJson["inherit"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            frame.inherit = inheritMap.at(frameJson.value("inherit", "normal"));
                            boneTimeline["inherit"].push_back(frame);
                        }
                    }
                    animationData.bones[boneName] = boneTimeline;
                }
            }
            if (animationJson.contains("ik")) {
                for (const auto& [ikName, ikJson] : animationJson["ik"].items()) {
                    Timeline ikTimeline; 
                    for (const auto& frameJson : ikJson) {
                        TimelineFrame frame;
                        frame.time = frameJson.value("time", 0.0f);
                        frame.value1 = frameJson.value("mix", 1.0f);
                        frame.value2 = frameJson.value("softness", 0.0f);
                        frame.bendPositive = frameJson.value("bendPositive", true); 
                        frame.compress = frameJson.value("compress", false);
                        frame.stretch = frameJson.value("stretch", false);
                        readCurve(frameJson, frame);
                        ikTimeline.push_back(frame);
                    }
                    animationData.ik[ikName] = ikTimeline;
                }
            }
            if (animationJson.contains("transform")) {
                for (const auto& [transformName, transformJson] : animationJson["transform"].items()) {
                    Timeline transformTimeline; 
                    for (const auto& frameJson : transformJson) {
                        TimelineFrame frame;
                        frame.time = frameJson.value("time", 0.0f);
                        frame.value1 = frameJson.value("mixRotate", 1.0f);
                        frame.value2 = frameJson.value("mixX", 1.0f); 
                        frame.value3 = frameJson.value("mixY", frame.value2);
                        frame.value4 = frameJson.value("mixScaleX", 1.0f);
                        frame.value5 = frameJson.value("mixScaleY", native ? 1.0f : frame.value4);
                        frame.value6 = frameJson.value("mixShearY", 1.0f);
                        readCurve(frameJson, frame);
                        transformTimeline.push_back(frame);
                    }
                    animationData.transform[transformName] = transformTimeline;
                }
            }
            if (animationJson.contains("path")) {
                for (const auto& [pathName, pathJson] : animationJson["path"].items()) {
                    MultiTimeline pathTimeline;
                    if (pathJson.contains("position")) {
                        readTimeline(pathJson["position"], pathTimeline["position"], 1, "value", "", 0.0f);
                    }
                    if (pathJson.contains("spacing")) {
                        readTimeline(pathJson["spacing"], pathTimeline["spacing"], 1, "value", "", 0.0f);
                    }
                    if (pathJson.contains("mix")) {
                        for (const auto& frameJson : pathJson["mix"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            frame.value1 = frameJson.value("mixRotate", 1.0f);
                            frame.value2 = frameJson.value("mixX", 1.0f);
                            frame.value3 = frameJson.value("mixY", frame.value2);
                            readCurve(frameJson, frame);
                            pathTimeline["mix"].push_back(frame);
                        }
                    }
                    animationData.path[pathName] = pathTimeline;
                }
            }
            if (animationJson.contains("physics")) {
                for (const auto& [physicsName, physicsJson] : animationJson["physics"].items()) {
                    MultiTimeline physicsTimeline;
                    if (physicsJson.contains("reset")) {
                        for (const auto& frameJson : physicsJson["reset"]) {
                            TimelineFrame frame;
                            frame.time = frameJson.value("time", 0.0f);
                            physicsTimeline["reset"].push_back(frame);
                        }
                    }
                    if (physicsJson.contains("inertia")) {
                        readTimeline(physicsJson["inertia"], physicsTimeline["inertia"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("strength")) {
                        readTimeline(physicsJson["strength"], physicsTimeline["strength"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("damping")) {
                        readTimeline(physicsJson["damping"], physicsTimeline["damping"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("mass")) {
                        readTimeline(physicsJson["mass"], physicsTimeline["mass"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("wind")) {
                        readTimeline(physicsJson["wind"], physicsTimeline["wind"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("gravity")) {
                        readTimeline(physicsJson["gravity"], physicsTimeline["gravity"], 1, "value", "", 0.0f);
                    }
                    if (physicsJson.contains("mix")) {
                        readTimeline(physicsJson["mix"], physicsTimeline["mix"], 1, "value", "", 1.0f);
                    }
                    animationData.physics[physicsName] = physicsTimeline;
                }
            }
            if (animationJson.contains("slider")) {
                for (const auto& [name, timelines] : animationJson["slider"].items()) {
                    for (const auto& [type, keys] : timelines.items()) {
                        if (type != "time" && type != "mix") throw std::runtime_error("Unknown slider timeline: " + type);
                        readTimeline(keys, animationData.slider[name][type], 1, "value", "", 1.0f);
                    }
                }
            }
            if (animationJson.contains("attachments")) {
                for (const auto& [skinName, skinJson] : animationJson["attachments"].items()) {
                    for (const auto& [slotName, slotJson] : skinJson.items()) {
                        for (const auto& [attachmentName, attachmentJson] : slotJson.items()) {
                            MultiTimeline attachmentTimeline;
                            if (attachmentJson.contains("deform")) {
                                for (const auto& frameJson : attachmentJson["deform"]) {
                                    TimelineFrame frame;
                                    frame.time = frameJson.value("time", 0.0f);
                                    if (frameJson.contains("vertices")) {
                                        frame.int1 = frameJson.value("offset", 0);
                                        frame.vertices = frameJson["vertices"].get<std::vector<float>>();
                                    }
                                    readCurve(frameJson, frame);
                                    attachmentTimeline["deform"].push_back(frame);
                                }
                            }
                            if (attachmentJson.contains("sequence")) {
                                float lastDelay = 0.0f; 
                                for (const auto& frameJson : attachmentJson["sequence"]) {
                                    TimelineFrame frame;
                                    frame.time = frameJson.value("time", 0.0f);
                                    frame.value1 = frameJson.value("delay", lastDelay);
                                    lastDelay = frame.value1;
                                    frame.int1 = frameJson.value("index", 0);
                                    frame.sequenceMode = sequenceModeMap.at(frameJson.value("mode", "hold"));
                                    attachmentTimeline["sequence"].push_back(frame);
                                }
                            }
                            animationData.attachments[skinName][slotName][attachmentName] = attachmentTimeline;
                        }
                    }
                }
            }
            if (animationJson.contains("drawOrder")) {
                for (const auto& frameJson : animationJson["drawOrder"]) {
                    animationData.drawOrder.push_back(readDrawOrderKey(frameJson));
                }
            }
            if (animationJson.contains("drawOrderFolder")) {
                for (const auto& folder : animationJson["drawOrderFolder"]) {
                    DrawOrderFolder drawOrder;
                    drawOrder.slots = folder.at("slots").get<std::vector<std::string>>();
                    for (const auto& key : folder.at("keys")) drawOrder.keys.push_back(readDrawOrderKey(key));
                    animationData.drawOrderFolders.push_back(std::move(drawOrder));
                }
            }
            if (animationJson.contains("events")) {
                for (const auto& frameJson : animationJson["events"]) {
                    TimelineFrame frame;
                    frame.time = frameJson.value("time", 0.0f);
                    frame.str1 = frameJson.at("name");
                    auto event = std::find_if(skeletonData.events.begin(), skeletonData.events.end(),
                        [&](const EventData& e) { return e.name == frame.str1; });
                    if (event == skeletonData.events.end()) throw std::runtime_error("Unknown event: " + *frame.str1);
                    const EventData& eventData = *event;
                    frame.int1 = frameJson.value("int", eventData.intValue);
                    frame.value1 = frameJson.value("float", eventData.floatValue);
                    frame.str2 = optionalString(frameJson, "string");
                    if (!frame.str2) frame.str2 = eventData.stringValue;
                    if (eventData.audioPath && !eventData.audioPath->empty()) {
                        frame.value2 = frameJson.value("volume", eventData.volume);
                        frame.value3 = frameJson.value("balance", eventData.balance);
                    }
                    animationData.events.push_back(frame);
                }
            }
            if (animationJson.contains("color")) {
                animationData.color = stringToColor(animationJson["color"], true);
                skeletonData.nonessential = true;
            }
            skeletonData.animations.push_back(std::move(animationData));
        }
    }

    return skeletonData;
}

}
