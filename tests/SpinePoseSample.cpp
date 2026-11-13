#include "HeadlessSpine.h"
#include "json.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace {
using namespace spine;
using Report = nlohmann::ordered_json;

std::unique_ptr<SkeletonData> loadSkeleton(const std::filesystem::path& path, HeadlessLoader& loader) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path.string());
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
#ifdef SPINE_REFERENCE_38
    auto* attachmentLoader = &loader;
#else
    auto& attachmentLoader = loader;
#endif
    if (path.extension() == ".skel") {
        SkeletonBinary reader(attachmentLoader);
        auto data = reader.readSkeletonData(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
        if (!data) throw std::runtime_error(reader.getError().buffer());
        return std::unique_ptr<SkeletonData>(data);
    }
    SkeletonJson reader(attachmentLoader);
    auto data = reader.readSkeletonData(bytes.c_str());
    if (!data) throw std::runtime_error(reader.getError().buffer());
    return std::unique_ptr<SkeletonData>(data);
}

bool keep(std::string_view selected, std::string_view channel) {
    return selected == "all" || selected == channel;
}

bool validChannel(std::string_view channel) {
    return channel == "all" || channel == "none" || channel == "rotate" ||
        channel == "translate" || channel == "scale" || channel == "shear";
}

void isolateTransformMixes(Skeleton& skeleton, std::string_view channel) {
    if (channel == "all") return;
#ifdef SPINE_REFERENCE_38
    auto& constraints = skeleton.getTransformConstraints();
    for (size_t i = 0; i < constraints.size(); ++i) {
        auto& constraint = *constraints[i];
        if (!keep(channel, "rotate")) constraint.setRotateMix(0);
        if (!keep(channel, "translate")) constraint.setTranslateMix(0);
        if (!keep(channel, "scale")) constraint.setScaleMix(0);
        if (!keep(channel, "shear")) constraint.setShearMix(0);
    }
#else
    const auto& constraints = skeleton.getConstraints();
    for (size_t i = 0; i < constraints.size(); ++i) {
        auto* constraint = dynamic_cast<TransformConstraint*>(constraints[i]);
        if (!constraint) continue;
        auto& pose = constraint->getPose();
        if (!keep(channel, "rotate")) pose.setMixRotate(0);
        if (!keep(channel, "translate")) {
            pose.setMixX(0);
            pose.setMixY(0);
        }
        if (!keep(channel, "scale")) {
            pose.setMixScaleX(0);
            pose.setMixScaleY(0);
        }
        if (!keep(channel, "shear")) pose.setMixShearY(0);
    }
#endif
}

Report executionOrder(Skeleton& skeleton, Report& updates) {
    Report result = Report::array();
#ifdef SPINE_REFERENCE_38
    auto& cache = skeleton.getUpdateCacheList();
    for (size_t i = 0; i < cache.size(); ++i) {
        if (auto* bone = dynamic_cast<Bone*>(cache[i])) {
            updates.push_back({"bone", bone->getData().getName().buffer()});
            continue;
        }
        const char* name = nullptr;
        if (auto* ik = dynamic_cast<IkConstraint*>(cache[i])) name = ik->getData().getName().buffer();
        else if (auto* transform = dynamic_cast<TransformConstraint*>(cache[i])) name = transform->getData().getName().buffer();
        else if (auto* path = dynamic_cast<PathConstraint*>(cache[i])) name = path->getData().getName().buffer();
        if (name) {
            result.push_back(name);
            updates.push_back({"constraint", name});
        }
    }
#else
    const auto& cache = skeleton.getUpdateCache();
    std::map<const Update*, const char*> boneNames;
    const auto& bones = skeleton.getBones();
    for (size_t i = 0; i < bones.size(); ++i)
        boneNames.emplace(&bones[i]->getAppliedPose(), bones[i]->getData().getName().buffer());
    for (size_t i = 0; i < cache.size(); ++i) {
        if (auto* constraint = dynamic_cast<Constraint*>(cache[i])) {
            const char* name = constraint->getData().getName().buffer();
            result.push_back(name);
            updates.push_back({"constraint", name});
        } else if (const auto bone = boneNames.find(cache[i]); bone != boneNames.end()) {
            updates.push_back({"bone", bone->second});
        }
    }
#endif
    return result;
}

void updateWorld(Skeleton& skeleton) {
#ifdef SPINE_REFERENCE_38
    skeleton.updateWorldTransform();
#else
    skeleton.updateWorldTransform(Physics_None);
#endif
}

void applyAnimation(Skeleton& skeleton, Animation& animation, float previous, float time) {
#ifdef SPINE_REFERENCE_38
    animation.apply(skeleton, previous, time, false, nullptr, 1, MixBlend_Setup, MixDirection_In);
#else
    animation.apply(skeleton, previous, time, false, nullptr, 1, MixFrom_Setup, false, false, false);
#endif
}

Report bones(Skeleton& skeleton) {
    Report result = Report::object();
    auto& list = skeleton.getBones();
    for (size_t i = 0; i < list.size(); ++i) {
        auto& bone = *list[i];
#ifdef SPINE_REFERENCE_38
        auto& world = bone;
        const float rotation = bone.getAppliedRotation();
#else
        auto& world = bone.getAppliedPose();
        world.validateLocalTransform(skeleton);
        const float rotation = world.getRotation();
#endif
        const float a = world.getA(), b = world.getB(), c = world.getC(), d = world.getD();
        result[bone.getData().getName().buffer()] = {
            {"world", {world.getWorldX(), world.getWorldY(), std::atan2(c, a) * 180.0 / std::numbers::pi,
                       std::hypot(a, c), std::hypot(b, d)}},
            {"matrix", {a, b, c, d}},
            {"appliedRotation", rotation},
            {"active", bone.isActive()}
        };
    }
    return result;
}

void setSkin(Skeleton& skeleton, const char* skinName) {
    if (!skinName) return;
#ifdef SPINE_REFERENCE_38
    auto* skin = skeleton.getData()->findSkin(skinName);
#else
    auto* skin = skeleton.getData().findSkin(skinName);
#endif
    if (!skin) throw std::runtime_error("Unknown skin: " + std::string(skinName));
    skeleton.setSkin(skin);
#ifdef SPINE_REFERENCE_38
    skeleton.setToSetupPose();
#else
    skeleton.setupPose();
#endif
}
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: SpinePoseSample skeleton [all|none|rotate|translate|scale|shear] [skin]\n";
        return 2;
    }
    try {
        const std::string_view channel = argc > 2 ? argv[2] : "all";
        if (!validChannel(channel)) throw std::runtime_error("Unknown transform mix channel");
        Bone::setYDown(false);
        HeadlessLoader loader;
        auto data = loadSkeleton(argv[1], loader);
        const char* skinName = argc == 4 ? argv[3] : nullptr;
#ifdef SPINE_REFERENCE_38
        auto* skeletonData = data.get();
#else
        auto& skeletonData = *data;
#endif
        Skeleton setup(skeletonData);
        setSkin(setup, skinName);
        isolateTransformMixes(setup, channel);
        updateWorld(setup);
        Report updates = Report::array();
        Report order = executionOrder(setup, updates);
        Report report = {{"version", data->getVersion().buffer()}, {"order", std::move(order)}, {"updates", std::move(updates)},
                         {"setup", bones(setup)}, {"animations", Report::object()}};
        auto& animations = data->getAnimations();
        for (size_t i = 0; i < animations.size(); ++i) {
            auto& animation = *animations[i];
            Skeleton skeleton(skeletonData);
            setSkin(skeleton, skinName);
            float previous = -1, duration = animation.getDuration();
            Report frames = Report::array();
            const int steps = static_cast<int>(std::ceil(duration * 60));
            for (int step = 0; step <= steps; ++step) {
                const float time = step == steps ? duration : step / 60.0f;
                applyAnimation(skeleton, animation, previous, time);
                isolateTransformMixes(skeleton, channel);
                updateWorld(skeleton);
                frames.push_back({{"time", time}, {"bones", bones(skeleton)}});
                previous = time;
            }
            report["animations"][animation.getName().buffer()] = std::move(frames);
        }
        std::cout << report.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
