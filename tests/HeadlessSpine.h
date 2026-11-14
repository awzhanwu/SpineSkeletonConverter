#pragma once

#include <spine/spine.h>

namespace spine {
SpineExtension* getDefaultExtension() {
    return new DefaultSpineExtension();
}
}

// Construct the runtime's attachments without textures, including meshes used by deform and path timelines.
class HeadlessLoader : public spine::AttachmentLoader {
    using Skin = spine::Skin;
    using String = spine::String;

#ifdef SPINE_REFERENCE_38
    spine::RegionAttachment* newRegionAttachment(Skin&, const String& name, const String&) override {
        return new (__FILE__, __LINE__) spine::RegionAttachment(name);
    }
    spine::MeshAttachment* newMeshAttachment(Skin&, const String& name, const String&) override {
        return new (__FILE__, __LINE__) spine::MeshAttachment(name);
    }
    spine::BoundingBoxAttachment* newBoundingBoxAttachment(Skin&, const String& name) override {
        return new (__FILE__, __LINE__) spine::BoundingBoxAttachment(name);
    }
    spine::PathAttachment* newPathAttachment(Skin&, const String& name) override {
        return new (__FILE__, __LINE__) spine::PathAttachment(name);
    }
    spine::PointAttachment* newPointAttachment(Skin&, const String& name) override {
        return new (__FILE__, __LINE__) spine::PointAttachment(name);
    }
    spine::ClippingAttachment* newClippingAttachment(Skin&, const String& name) override {
        return new (__FILE__, __LINE__) spine::ClippingAttachment(name);
    }
    void configureAttachment(spine::Attachment*) override {}
#else
    spine::RegionAttachment* newRegionAttachment(Skin&, const String&, const String& name, const String&, spine::Sequence* sequence) override {
        return new (__FILE__, __LINE__) spine::RegionAttachment(name, sequence);
    }
    spine::MeshAttachment* newMeshAttachment(Skin&, const String&, const String& name, const String&, spine::Sequence* sequence) override {
        return new (__FILE__, __LINE__) spine::MeshAttachment(name, sequence);
    }
    spine::BoundingBoxAttachment* newBoundingBoxAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) spine::BoundingBoxAttachment(name);
    }
    spine::PathAttachment* newPathAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) spine::PathAttachment(name);
    }
    spine::PointAttachment* newPointAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) spine::PointAttachment(name);
    }
    spine::ClippingAttachment* newClippingAttachment(Skin&, const String&, const String& name) override {
        return new (__FILE__, __LINE__) spine::ClippingAttachment(name);
    }
#endif
};
