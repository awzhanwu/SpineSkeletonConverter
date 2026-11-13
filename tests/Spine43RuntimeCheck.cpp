#include <spine/spine.h>

#include "json.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace spine {
SpineExtension* getDefaultExtension() {
    return new DefaultSpineExtension();
}
}

namespace {
using namespace spine;
using Json = nlohmann::json;

// The readers still construct real runtime attachments and sequences. No atlas
// or renderer is needed to compare the skeleton and animation data.
class HeadlessLoader : public AttachmentLoader {
    RegionAttachment* newRegionAttachment(Skin&, const String&, const String& name, const String&, Sequence* sequence) override {
        return new (__FILE__, __LINE__) RegionAttachment(name, sequence);
    }
    MeshAttachment* newMeshAttachment(Skin&, const String&, const String& name, const String&, Sequence* sequence) override {
        return new (__FILE__, __LINE__) MeshAttachment(name, sequence);
    }
    BoundingBoxAttachment* newBoundingBoxAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) BoundingBoxAttachment(name);
    }
    PathAttachment* newPathAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) PathAttachment(name);
    }
    PointAttachment* newPointAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) PointAttachment(name);
    }
    ClippingAttachment* newClippingAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) ClippingAttachment(name);
    }
};

std::string str(const String& value) {
    return value.isEmpty() ? "" : value.buffer();
}

template <class T>
Json name(T* value) {
    return value ? Json(str(value->getName())) : Json(nullptr);
}

template <class T, class F>
Json array(const Array<T>& values, F convert) {
    Json result = Json::array();
    for (size_t i = 0; i < values.size(); ++i) result.push_back(convert(values[i]));
    return result;
}

template <class T>
Json numbers(const Array<T>& values) {
    return array(values, [](T value) { return value; });
}

Json color(const Color& value) {
    return {std::lround(value.r * 255), std::lround(value.g * 255),
            std::lround(value.b * 255), std::lround(value.a * 255)};
}

Json sequence(Sequence& value) {
    return {value.getRegions().size(), value.hasPathSuffix(), value.getStart(),
            value.getDigits(), value.getSetupIndex()};
}

Json event(Event& value) {
    Json result = {str(value.getData().getName()), value.getInt(), value.getFloat(), str(value.getString())};
    if (!value.getData().getAudioPath().isEmpty()) {
        result.push_back(value.getVolume());
        result.push_back(value.getBalance());
    }
    return result;
}

Json attachment(Attachment& value, const std::string& slot) {
    Json result = {{"type", value.getRTTI().getClassName()}, {"name", str(value.getName())},
                   {"timelineSlots", numbers(value.getTimelineSlots())}};
    if (auto* vertex = dynamic_cast<VertexAttachment*>(&value)) {
        result["worldVerticesLength"] = vertex->getWorldVerticesLength();
        result["bones"] = numbers(vertex->getBones());
        result["vertices"] = numbers(vertex->getVertices());
    }
    if (auto* region = dynamic_cast<RegionAttachment*>(&value)) {
        result["path"] = str(region->getPath());
        result["color"] = color(region->getColor());
        result["sequence"] = sequence(region->getSequence());
        result["transform"] = {region->getX(), region->getY(), region->getRotation(),
                               region->getScaleX(), region->getScaleY(), region->getWidth(), region->getHeight()};
    } else if (auto* mesh = dynamic_cast<MeshAttachment*>(&value)) {
        result["path"] = str(mesh->getPath());
        result["color"] = color(mesh->getColor());
        result["sequence"] = sequence(mesh->getSequence());
        result["size"] = {mesh->getWidth(), mesh->getHeight()};
        result["hull"] = mesh->getHullLength();
        result["uvs"] = numbers(mesh->getRegionUVs());
        result["triangles"] = numbers(mesh->getTriangles());
        result["edges"] = numbers(mesh->getEdges());
        result["source"] = name(mesh->getSourceMesh());
        result["timelineAttachment"] = name(mesh->getTimelineAttachment());
    } else if (auto* box = dynamic_cast<BoundingBoxAttachment*>(&value)) {
        result["color"] = color(box->getColor());
    } else if (auto* path = dynamic_cast<PathAttachment*>(&value)) {
        result["color"] = color(path->getColor());
        result["closed"] = path->getClosed();
        result["constantSpeed"] = path->getConstantSpeed();
        result["lengths"] = numbers(path->getLengths());
    } else if (auto* point = dynamic_cast<PointAttachment*>(&value)) {
        result["color"] = color(point->getColor());
        result["transform"] = {point->getX(), point->getY(), point->getRotation()};
    } else if (auto* clip = dynamic_cast<ClippingAttachment*>(&value)) {
        result["color"] = color(clip->getColor());
        // Both forms keep clipping active through the remaining draw order.
        result["end"] = clip->getEndSlot() ? name(clip->getEndSlot()) : Json(slot);
        result["convex"] = clip->getConvex();
        result["inverse"] = clip->getInverse();
    }
    return result;
}

Json constraint(ConstraintData& value) {
    Json result = {{"name", str(value.getName())}, {"type", value.getRTTI().getClassName()},
                   {"skin", value.getSkinRequired()}};
    if (auto* ik = dynamic_cast<IkConstraintData*>(&value)) {
        auto& pose = ik->getSetupPose();
        result["bones"] = array(ik->getBones(), name<BoneData>);
        result["target"] = name(&ik->getTarget());
        result["scaleY"] = ik->getScaleYMode();
        result["pose"] = {pose.getMix(), pose.getSoftness(), pose.getBendDirection(), pose.getCompress(), pose.getStretch()};
    } else if (auto* transform = dynamic_cast<TransformConstraintData*>(&value)) {
        auto& pose = transform->getSetupPose();
        result["bones"] = array(transform->getBones(), name<BoneData>);
        result["source"] = name(&transform->getSource());
        result["flags"] = {transform->getLocalSource(), transform->getLocalTarget(), transform->getAdditive(), transform->getClamp()};
        result["offsets"] = {transform->getOffsetRotation(), transform->getOffsetX(), transform->getOffsetY(),
                             transform->getOffsetScaleX(), transform->getOffsetScaleY(), transform->getOffsetShearY()};
        result["mixes"] = {pose.getMixRotate(), pose.getMixX(), pose.getMixY(), pose.getMixScaleX(), pose.getMixScaleY(), pose.getMixShearY()};
        result["properties"] = array(transform->getProperties(), [](FromProperty* from) {
            auto destinations = array(from->_to, [](ToProperty* to) {
                return Json::array({to->getRTTI().getClassName(), to->_offset, to->_max, to->_scale});
            });
            return Json::array({from->getRTTI().getClassName(), from->_offset, destinations});
        });
    } else if (auto* path = dynamic_cast<PathConstraintData*>(&value)) {
        auto& pose = path->getSetupPose();
        result["bones"] = array(path->getBones(), name<BoneData>);
        result["slot"] = name(&path->getSlot());
        result["modes"] = {path->getPositionMode(), path->getSpacingMode(), path->getRotateMode()};
        result["pose"] = {path->getOffsetRotation(), pose.getPosition(), pose.getSpacing(), pose.getMixRotate(), pose.getMixX(), pose.getMixY()};
    } else if (auto* physics = dynamic_cast<PhysicsConstraintData*>(&value)) {
        auto& pose = physics->getSetupPose();
        result["bone"] = name(&physics->getBone());
        result["data"] = {physics->getX(), physics->getY(), physics->getRotate(), physics->getScaleX(),
                          physics->getShearX(), physics->getLimit(), physics->getStep(), physics->getScaleYMode()};
        result["pose"] = {pose.getInertia(), pose.getStrength(), pose.getDamping(), pose.getMassInverse(),
                          pose.getWind(), pose.getGravity(), pose.getMix()};
        result["globals"] = {physics->getInertiaGlobal(), physics->getStrengthGlobal(), physics->getDampingGlobal(),
                             physics->getMassGlobal(), physics->getWindGlobal(), physics->getGravityGlobal(), physics->getMixGlobal()};
    } else if (auto* slider = dynamic_cast<SliderData*>(&value)) {
        auto& pose = slider->getSetupPose();
        result["bone"] = name(slider->getBone());
        result["animation"] = name(&slider->getAnimation());
        result["data"] = {slider->getAdditive(), slider->getLoop(), slider->getLocal(),
                          slider->getScale(), slider->getOffset(), slider->getMax()};
        result["pose"] = {pose.getTime(), pose.getMix()};
        if (auto* property = slider->getProperty()) {
            result["property"] = {property->getRTTI().getClassName(), property->_offset};
        }
    }
    return result;
}

using AttachmentNames = std::map<Attachment*, Json>;

Json animation(Animation& value, const AttachmentNames& attachments) {
    Json timelines = Json::array();
    for (size_t i = 0; i < value.getTimelines().size(); ++i) {
        Timeline* timeline = value.getTimelines()[i];
        Json entry = {{"type", timeline->getRTTI().getClassName()}, {"frames", numbers(timeline->getFrames())}};
        if (auto* bone = dynamic_cast<BoneTimeline*>(timeline)) entry["bone"] = bone->getBoneIndex();
        if (auto* slot = dynamic_cast<SlotTimeline*>(timeline)) entry["slot"] = slot->getSlotIndex();
        if (auto* constraint = dynamic_cast<ConstraintTimeline*>(timeline)) entry["constraint"] = constraint->getConstraintIndex();
        if (auto* curve = dynamic_cast<CurveTimeline*>(timeline)) {
            auto& curves = curve->getCurves();
            // JSON preallocates a Bezier for every segment. Compare the populated data.
            size_t count = curves.size();
            while (count > timeline->getFrameCount() && curves[count - 1] == 0) --count;
            entry["curves"] = Json::array();
            for (size_t j = 0; j < count; ++j) entry["curves"].push_back(curves[j]);
        }
        if (auto* swap = dynamic_cast<AttachmentTimeline*>(timeline)) {
            entry["names"] = array(swap->getAttachmentNames(), str);
        } else if (auto* deform = dynamic_cast<DeformTimeline*>(timeline)) {
            entry["attachment"] = attachments.at(&deform->getAttachment());
            entry["vertices"] = array(deform->getVertices(), numbers<float>);
        } else if (auto* sequence = dynamic_cast<SequenceTimeline*>(timeline)) {
            entry["attachment"] = attachments.at(&sequence->getAttachment());
        } else if (auto* drawOrder = dynamic_cast<DrawOrderTimeline*>(timeline)) {
            entry["order"] = array(drawOrder->getDrawOrders(), numbers<int>);
        } else if (auto* folder = dynamic_cast<DrawOrderFolderTimeline*>(timeline)) {
            entry["folder"] = numbers(folder->getSlots());
            entry["order"] = array(folder->getDrawOrders(), numbers<int>);
        } else if (auto* events = dynamic_cast<EventTimeline*>(timeline)) {
            entry["events"] = array(events->getEvents(), [](Event* e) { return event(*e); });
        }
        timelines.push_back(std::move(entry));
    }
    auto identity = [](const Json& entry) {
        return Json::array({entry.at("type"), entry.value("bone", -1), entry.value("slot", -1),
                            entry.value("constraint", -1), entry.value("attachment", Json()), entry.value("folder", Json())});
    };
    std::stable_sort(timelines.begin(), timelines.end(), [&](const Json& a, const Json& b) {
        return identity(a) < identity(b);
    });
    auto bones = numbers(value.getBones());
    std::sort(bones.begin(), bones.end());
    return {{"duration", value.getDuration()}, {"color", color(value.getColor())}, {"bones", bones}, {"timelines", timelines}};
}

Json snapshot(SkeletonData& value) {
    Json result = {
        {"version", str(value.getVersion())},
        {"bounds", {value.getX(), value.getY(), value.getWidth(), value.getHeight()}},
        {"referenceScale", value.getReferenceScale()}, {"fps", value.getFps()},
        {"images", str(value.getImagesPath())}, {"audio", str(value.getAudioPath())},
        {"bones", array(value.getBones(), [](BoneData* bone) {
            auto& p = bone->getSetupPose();
            return Json{{"name", name(bone)}, {"parent", name(bone->getParent())}, {"length", bone->getLength()},
                        {"pose", {p.getRotation(), p.getX(), p.getY(), p.getScaleX(), p.getScaleY(), p.getShearX(), p.getShearY(), p.getInherit()}},
                        {"skin", bone->getSkinRequired()}, {"color", color(bone->getColor())}, {"visible", bone->getVisible()},
                        {"icon", {str(bone->getIcon()), bone->getIconSize(), bone->getIconRotation()}}};
        })},
        {"slots", array(value.getSlots(), [](SlotData* slot) {
            auto& p = slot->getSetupPose();
            return Json{{"name", name(slot)}, {"bone", name(&slot->getBoneData())},
                        {"attachment", str(slot->getAttachmentName())}, {"color", color(p.getColor())},
                        {"dark", p.hasDarkColor() ? color(p.getDarkColor()) : Json()},
                        {"blend", slot->getBlendMode()}, {"visible", slot->getVisible()}};
        })},
        {"constraints", array(value.getConstraints(), [](ConstraintData* c) { return constraint(*c); })},
        {"skins", Json::object()}, {"animations", Json::object()}, {"events", Json::object()}
    };
    AttachmentNames attachments;
    for (size_t i = 0; i < value.getSkins().size(); ++i) {
        auto& skin = *value.getSkins()[i];
        auto entries = skin.getAttachments();
        Json slots = Json::object();
        while (entries.hasNext()) {
            auto& entry = entries.next();
            auto slotName = str(value.getSlots()[entry._slotIndex]->getName());
            auto placeholder = str(entry._placeholder);
            slots[slotName][placeholder] = attachment(*entry._attachment, slotName);
            attachments.emplace(entry._attachment, Json::array({str(skin.getName()), slotName, placeholder}));
        }
        if (slots.empty() && &skin == value.getDefaultSkin()) continue;
        result["skins"][str(skin.getName())] = {
            {"color", color(skin.getColor())}, {"bones", array(skin.getBones(), name<BoneData>)},
            {"constraints", array(skin.getConstraints(), [](ConstraintData* c) {
                return Json::array({c->getRTTI().getClassName(), str(c->getName())});
            })},
            {"attachments", std::move(slots)}
        };
    }
    for (size_t i = 0; i < value.getEvents().size(); ++i) {
        auto& e = *value.getEvents()[i];
        result["events"][str(e.getName())] = {str(e.getAudioPath()), event(e.getSetupPose())};
    }
    for (size_t i = 0; i < value.getAnimations().size(); ++i) {
        auto& a = *value.getAnimations()[i];
        result["animations"][str(a.getName())] = animation(a, attachments);
    }
    return result;
}

void compare(const Json& expected, const Json& actual, const std::string& path, double tolerance = 0.00002) {
    if (expected.is_number_float() && actual.is_number_float()) {
        double a = expected, b = actual;
        if (std::abs(a - b) <= tolerance + 0.000002 * std::max(std::abs(a), std::abs(b))) return;
    } else if (expected.is_object() && actual.is_object() && expected.size() == actual.size()) {
        if (auto type = expected.find("type"); type != expected.end() && *type == "AlphaTimeline")
            tolerance = 0.5 / 255 + 0.00002;
        for (const auto& [key, value] : expected.items()) {
            if (!actual.contains(key)) throw std::runtime_error(path + "/" + key + " missing from SKEL");
            compare(value, actual.at(key), path + "/" + key, tolerance);
        }
        return;
    } else if (expected.is_array() && actual.is_array() && expected.size() == actual.size()) {
        for (size_t i = 0; i < expected.size(); ++i) compare(expected[i], actual[i], path + "/" + std::to_string(i), tolerance);
        return;
    } else if (expected == actual) {
        return;
    }
    throw std::runtime_error(path + " differs. JSON " + expected.dump().substr(0, 250) + ", SKEL " + actual.dump().substr(0, 250));
}

std::string readFile(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error(std::string("Cannot open ") + filename);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string binaryHash(std::string_view base64) {
    constexpr std::string_view digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint64_t hash = 0;
    unsigned int bits = 0, buffer = 0;
    for (char c : base64) {
        const auto digit = digits.find(c);
        if (digit == std::string_view::npos) break;
        buffer = (buffer << 6) | static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            hash = (hash << 8) | ((buffer >> bits) & 0xff);
        }
    }
    return hash == 0 ? "" : std::to_string(std::bit_cast<int64_t>(hash));
}
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: Spine43RuntimeCheck reference.json converted.skel\n";
        return 2;
    }
    try {
        HeadlessLoader loader;
        spine::SkeletonJson jsonReader(loader);
        spine::SkeletonBinary binaryReader(loader);
        auto json = readFile(argv[1]);
        auto binary = readFile(argv[2]);
        std::unique_ptr<spine::SkeletonData> expected(jsonReader.readSkeletonData(json.c_str()));
        if (!expected) throw std::runtime_error("Official JSON reader: " + str(jsonReader.getError()));
        std::cerr << "Reading with official SkeletonBinary: " << argv[2] << std::endl;
        std::unique_ptr<spine::SkeletonData> actual(binaryReader.readSkeletonData(
            reinterpret_cast<const unsigned char*>(binary.data()), static_cast<int>(binary.size())));
        if (!actual) throw std::runtime_error("Official binary reader: " + str(binaryReader.getError()));
        compare(snapshot(*expected), snapshot(*actual), "");
        compare(binaryHash(str(expected->getHash())), str(actual->getHash()), "/hash");
        std::cout << "Official 4.3 JSON and SKEL data match\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
