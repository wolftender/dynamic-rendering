#include <queue>
#include <stb_image/stb_image.h>
#include <glm/gtx/quaternion.hpp>

#include "logger.hpp"
#include "model.hpp"

namespace graphics {

template <>
auto Model::getResource<Renderer::TextureId>(Renderer *renderer, Renderer::TextureId handle)
    -> const RendererTexture * {
    return renderer->getTexture(handle);
}

template <>
auto Model::getResource<Renderer::MeshId>(Renderer *renderer, Renderer::MeshId handle) -> const Renderer::Mesh * {
    return renderer->getMesh(handle);
}

template <>
auto Model::getResource<Renderer::AnimatedMeshId>(Renderer *renderer, Renderer::AnimatedMeshId handle)
    -> const Renderer::Mesh * {
    return renderer->getAnimMesh(handle);
}

template <> auto Model::deleteResource<Renderer::TextureId>(Renderer *renderer, Renderer::TextureId handle) -> void {
    renderer->deleteTexture(handle);
}

template <> auto Model::deleteResource<Renderer::MeshId>(Renderer *renderer, Renderer::MeshId handle) -> void {
    renderer->deleteMesh(handle);
}

template <>
auto Model::deleteResource<Renderer::AnimatedMeshId>(Renderer *renderer, Renderer::AnimatedMeshId handle) -> void {
    renderer->deleteAnimMesh(handle);
}

auto Model::Node::addMesh(MeshId id) -> void {
    const auto is_first_drawable = (meshes_.size() == 0) && (animated_meshes_.size() == 0);
    meshes_.push_back(std::move(id));

    if (is_first_drawable) {
        model_->mesh_nodes_.push_back(self_);
    }
}

auto Model::Node::addAnimMesh(AnimatedMeshId id) -> void {
    const auto is_first_drawable = (meshes_.size() == 0) && (animated_meshes_.size() == 0);
    animated_meshes_.push_back(std::move(id));

    if (is_first_drawable) {
        model_->mesh_nodes_.push_back(self_);
    }
}

auto Model::Pose::Node::setTranslation(const glm::fvec3 &translation) -> void {
    translation_ = translation;
    pose_->recomputeTransformSubtree(self_);
}

auto Model::Pose::Node::setScale(const glm::fvec3 &scale) -> void {
    scale_ = scale;
    pose_->recomputeTransformSubtree(self_);
}

auto Model::Pose::Node::setRotation(const glm::fquat &rotation) -> void {
    rotation_ = rotation;
    pose_->recomputeTransformSubtree(self_);
}

auto Model::Pose::Node::setTranslationSilent(const glm::fvec3 &translation) -> void { translation_ = translation; }
auto Model::Pose::Node::setScaleSilent(const glm::fvec3 &scale) -> void { scale_ = scale; }
auto Model::Pose::Node::setRotationSilent(const glm::fquat &rotation) -> void { rotation_ = rotation; }

auto Model::Pose::Node::setTransform(const glm::fvec3 &translation, const glm::fvec3 &scale, const glm::fquat &rotation)
    -> void {
    translation_ = translation;
    scale_ = scale;
    rotation_ = rotation;

    pose_->recomputeTransformSubtree(self_);
}

auto Model::Pose::getNode(NodeId handle) -> Node * {
    if (handle.index() >= nodes_.size()) {
        return nullptr;
    }

    return &nodes_[handle.index()];
}

auto Model::Pose::getNode(NodeId handle) const -> const Node * {
    if (handle.index() >= nodes_.size()) {
        return nullptr;
    }

    return &nodes_[handle.index()];
}

auto Model::Pose::fromModel(const Model &model) -> std::unique_ptr<Pose> {
    std::unique_ptr<Pose> pose{new (std::nothrow) Pose()};
    if (!pose) {
        return nullptr;
    }

    pose->renderer_ = model.renderer_;
    pose->nodes_.reserve(model.nodes_.size());

    for (const auto &node : model.nodes_) {
        Pose::Node pose_node{pose.get(), node.self()};

        pose_node.translation_ = node.translation();
        pose_node.scale_ = node.scale();
        pose_node.rotation_ = node.rotation();
        pose_node.parent_ = node.parent();
        pose_node.children_ = node.children();

        pose->nodes_.emplace_back(std::move(pose_node));
    }

    pose->recomputeTransformSubtree(pose->root());

    for (const auto &anim_mesh : model.animated_meshes_) {
        const auto actor_mesh_id = pose->renderer_->createActorMesh(anim_mesh.handle().id());
        if (!actor_mesh_id.has_value()) {
            LogError("model: failed to allocate actor mesh");
            return nullptr;
        }

        pose->actor_meshes_.push_back(actor_mesh_id.value());

        // fill with bind pose matrices
        const auto &skin = model.skins_[anim_mesh.skin().index()];

        auto actor_mesh = pose->renderer_->getActorMesh(actor_mesh_id.value());
        auto bone_array = actor_mesh->skinningBuffer().bones;

        std::fill(bone_array, bone_array + skin.nodes().size(), glm::fmat4x4{1.0f});
    }

    return pose;
}

auto Model::Pose::recomputeTransformSubtree(NodeId root) -> void {
    glm::fmat4x4 parent_matrix{1.0f};

    auto node = getNode(root);
    if (!node) {
        return;
    }

    if (node->parent_.has_value()) {
        const auto parent = getNode(node->parent_.value());
        if (parent) {
            parent_matrix = parent->transform();
        }
    }

    // T * R * S
    const auto translation = glm::translate(glm::fmat4x4{1.0f}, node->translation());
    const auto rotation = glm::toMat4(node->rotation());
    const auto scale = glm::scale(glm::fmat4x4{1.0f}, node->scale());

    node->transform_ = parent_matrix * translation * rotation * scale;

    for (auto &child : node->children_) {
        recomputeTransformSubtree(child);
    }
}

Model::Pose::~Pose() noexcept {
    if (renderer_) {
        for (auto &actor_mesh : actor_meshes_) {
            renderer_->deleteActorMesh(actor_mesh);
        }
    }
}

Model::Model(Renderer *renderer) : renderer_{renderer} {
    nodes_.emplace_back(
        Node{
            this, NodeId{0ul}, "(root)", std::nullopt, glm::fvec3{0.0f, 0.0f, 0.0f}, glm::fvec3{1.0f, 1.0f, 1.0f},
            glm::fquat{1.0f, 0.0f, 0.0f, 0.0f}});
}

auto Model::createPose() const -> std::unique_ptr<Pose> { return Pose::fromModel(*this); }

auto Model::addMeshImpl(
    std::span<const Renderer::StaticVertex> vertices, std::span<const uint32_t> indices, MaterialId material)
    -> std::optional<MeshId> {
    auto mesh_rc = renderer_->createMesh(vertices, indices);
    if (!mesh_rc.has_value()) {
        return std::nullopt;
    }

    meshes_.emplace_back(Mesh{this, RendererResource<Renderer::MeshId>{renderer_, mesh_rc.value()}, material});
    return MeshId{static_cast<uint32_t>(meshes_.size() - 1)};
}

auto Model::addAnimMeshImpl(
    std::span<const Renderer::SkinnedVertex> vertices, std::span<const uint32_t> indices, MaterialId material,
    SkinId skin) -> std::optional<AnimatedMeshId> {
    auto mesh_rc = renderer_->createAnimatedMesh(vertices, indices);
    if (!mesh_rc.has_value()) {
        return std::nullopt;
    }

    animated_meshes_.emplace_back(
        AnimatedMesh{
            this, RendererResource<Renderer::AnimatedMeshId>{renderer_, mesh_rc.value()}, material, std::move(skin)});
    return AnimatedMeshId{static_cast<uint32_t>(animated_meshes_.size()) - 1};
}

auto Model::addRgbaTextureImpl(uint32_t width, uint32_t height, std::span<const uint8_t> data)
    -> std::optional<TextureId> {
    RendererTexture::RgbaDescription desc = {
        .width = width,
        .height = height,
        .min_filter = RendererTexture::MinFilter::eLinear,
        .mag_filter = RendererTexture::MagFilter::eLinear,
        .init_data = data,
    };

    auto texture_rc = renderer_->createRgbaTexture(desc);
    if (!texture_rc.has_value()) {
        return std::nullopt;
    }

    textures_.emplace_back(Texture{this, RendererResource<Renderer::TextureId>{renderer_, texture_rc.value()}});
    return TextureId{static_cast<uint32_t>(textures_.size() - 1)};
}

auto Model::addSkin(Skin &&skin) -> std::optional<SkinId> {
    skins_.emplace_back(std::move(skin));
    return SkinId{static_cast<uint32_t>(skins_.size() - 1)};
}

auto Model::addMaterial(const Material::Description &desc) -> std::optional<MaterialId> {
    materials_.emplace_back(Material{this, desc.diffuse, desc.normal, desc.specular});
    return MaterialId{static_cast<uint32_t>(materials_.size() - 1)};
}

auto Model::addNode(NodeId parent, std::string_view name) -> std::optional<NodeId> {
    auto node = getNode(parent);
    if (!node) {
        return std::nullopt;
    }

    NodeId id{static_cast<uint32_t>(nodes_.size())};
    nodes_.emplace_back(
        Node{
            this, id, name, parent, glm::fvec3{0.0f, 0.0f, 0.0f}, glm::fvec3{1.0f, 1.0f, 1.0f},
            glm::fquat{1.0f, 0.0f, 0.0f, 0.0f}});

    node = getNode(parent);
    node->children_.push_back(id);

    return id;
}

auto Model::addAnimation(Animation &&animation) -> std::optional<AnimationId> {
    animations_.emplace_back(std::move(animation));
    return AnimationId{static_cast<uint32_t>(animations_.size())};
}

auto Model::getMesh(MeshId handle) -> Mesh * {
    if (handle.index() >= meshes_.size()) {
        return nullptr;
    }

    return &meshes_[handle.index()];
}

auto Model::getAnimMesh(AnimatedMeshId handle) -> AnimatedMesh * {
    if (handle.index() >= animated_meshes_.size()) {
        return nullptr;
    }

    return &animated_meshes_[handle.index()];
}

auto Model::getNode(NodeId handle) -> Node * {
    if (handle.index() >= nodes_.size()) {
        return nullptr;
    }

    return &nodes_[handle.index()];
}

auto Model::getTexture(TextureId handle) -> Texture * {
    if (handle.index() >= textures_.size()) {
        return nullptr;
    }

    return &textures_[handle.index()];
}

auto Model::getMaterial(MaterialId handle) -> Material * {
    if (handle.index() >= materials_.size()) {
        return nullptr;
    }

    return &materials_[handle.index()];
}

auto Model::getAnimation(AnimationId handle) -> Animation * {
    if (handle.index() >= materials_.size()) {
        return nullptr;
    }

    return &animations_[handle.index()];
}

auto Model::getMesh(MeshId handle) const -> const Mesh * {
    if (handle.index() >= meshes_.size()) {
        return nullptr;
    }

    return &meshes_[handle.index()];
}

auto Model::getAnimMesh(AnimatedMeshId handle) const -> const AnimatedMesh * {
    if (handle.index() >= animated_meshes_.size()) {
        return nullptr;
    }

    return &animated_meshes_[handle.index()];
}

auto Model::getNode(NodeId handle) const -> const Node * {
    if (handle.index() >= nodes_.size()) {
        return nullptr;
    }

    return &nodes_[handle.index()];
}

auto Model::getTexture(TextureId handle) const -> const Texture * {
    if (handle.index() >= textures_.size()) {
        return nullptr;
    }

    return &textures_[handle.index()];
}

auto Model::getMaterial(MaterialId handle) const -> const Material * {
    if (handle.index() >= materials_.size()) {
        return nullptr;
    }

    return &materials_[handle.index()];
}

auto Model::getAnimation(AnimationId handle) const -> const Animation * {
    if (handle.index() >= materials_.size()) {
        return nullptr;
    }

    return &animations_[handle.index()];
}

auto Model::render(Renderer &renderer, const Pose &pose, const glm::fmat4x4 &world) const -> void {
    if (&renderer != renderer_) {
        return;
    }

    for (const auto &node_id : mesh_nodes_) {
        const auto &node = nodes_[node_id.index()];
        const auto &pose_node = pose.nodes_[node_id.index()];
        const auto matrix = world * pose_node.transform();

        for (const auto &mesh_id : node.meshes()) {
            const auto &mesh = meshes_[mesh_id.index()];
            const auto &material = materials_[mesh.material().index()];

            Renderer::OpaqueDrawDescription desc = {
                .mesh = mesh.handle().id(),
                .world_matrix = matrix,
            };

            if (material.diffuse().has_value()) {
                desc.diffuse_map = textures_[material.diffuse()->index()].handle().id();
            }

            if (material.normal().has_value()) {
                desc.normal_map = textures_[material.normal()->index()].handle().id();
            }

            renderer.drawOpaqueMesh(std::move(desc));
        }

        for (const auto &anim_mesh_id : node.animatedMeshes()) {
            const auto &anim_mesh = animated_meshes_[anim_mesh_id.index()];
            const auto &material = materials_[anim_mesh.material().index()];

            // this is an animated mesh -> we need to update the matrices in the buffer
            const auto actor_mesh_id = pose.actor_meshes_[anim_mesh_id.index()];
            const auto &skin = skins_[anim_mesh.skin_.index()];
            renderer_->withActorMeshMut(actor_mesh_id, [&](Renderer::ActorMesh &actor_mesh) {
                for (uint32_t bone_id = 0; bone_id < skin.nodes().size(); ++bone_id) {
                    const auto &bone = skin.nodes()[bone_id];
                    const auto &bone_node = pose.nodes_[bone.node.index()];
                    glm::fmat4x4 joint_matrix = bone_node.transform() * bone.inverse_bind;

                    actor_mesh.skinningBuffer().bones[bone_id] = joint_matrix;
                }
            });

            Renderer::SkinnedDrawDescription desc = {
                .skinned_mesh = actor_mesh_id,
                .world_matrix = world,
            };

            if (material.diffuse().has_value()) {
                desc.diffuse_map = textures_[material.diffuse()->index()].handle().id();
            }

            if (material.normal().has_value()) {
                desc.normal_map = textures_[material.normal()->index()].handle().id();
            }

            renderer.drawSkinnedMesh(std::move(desc));
        }
    }
}

auto Model::Controller::setAnimation(AnimationId id) -> void {
    animation_ = id;
    time_ = 0.0f;
    translation_data_.clear();
    rotation_data_.clear();
    scale_data_.clear();

    initializeAnimationData();
}

auto Model::Controller::integrate(float delta_time) -> void {
    time_ = time_ + delta_time;

    model_->withAnimation(animation_, [&](const Animation &animation) {
        if (loop_ && time_ > animation.duration()) {
            time_ = std::fmod(time_, animation.duration());
            resetAnimationData(animation);
        }

        updateAnimation(animation);
    });
}

auto Model::Controller::seek(float time) -> void {
    model_->withAnimation(animation_, [&](const Animation &animation) {
        time_ = loop_ ? std::fmod(std::max(0.0f, time), animation.duration()) : std::max(0.0f, time);
        resetAnimationData(animation);
        updateAnimation(animation);
    });
}

auto Model::Controller::resetAnimationData(const Animation &animation) -> void {
    animation.iterateChannels<Animation::TargetProperty::eTranslation>(
        [&](uint32_t channel_id, const TranslationChannel &channel) {
        resetChannelData(translation_data_, channel_id, channel);
    });

    animation.iterateChannels<Animation::TargetProperty::eRotation>(
        [&](uint32_t channel_id, const RotationChhannel &channel) {
        resetChannelData(rotation_data_, channel_id, channel);
    });

    animation.iterateChannels<Animation::TargetProperty::eScale>(
        [&](uint32_t channel_id, const ScaleChannel &channel) { resetChannelData(scale_data_, channel_id, channel); });
}

auto Model::Controller::initializeAnimationData() -> void {
    model_->withAnimation(animation_, [&](const Animation &animation) {
        translation_data_.resize(animation.numTranslationChannels());
        rotation_data_.resize(animation.numRotationChannels());
        scale_data_.resize(animation.numScaleChannels());

        resetAnimationData(animation);
    });
}

using KeyframeVec3 = Model::Animation::template Keyframe<glm::fvec3>;
using KeyframeQuat = Model::Animation::template Keyframe<glm::fquat>;

inline auto interpolate(
    Model::Animation::InterpolationMode mode, const KeyframeVec3 &k0, const KeyframeVec3 &k1, float t,
    glm::fvec3 *result) -> void {
    const auto &v0 = k0.value;
    const auto &v1 = k1.value;
    const auto t0 = k0.time;
    const auto t1 = k1.time;
    float l = (t1 - t0);
    float _t = (glm::clamp(t, t0, t1) - t0) / l;

    switch (mode) {
    case Model::Animation::InterpolationMode::eLinear:
        *result = glm::mix(v0, v1, _t);
        break;
    case Model::Animation::InterpolationMode::eCubic:
    case Model::Animation::InterpolationMode::eStep:
        *result = v1;
        break;
    }
}

inline auto interpolate(
    Model::Animation::InterpolationMode mode, const KeyframeQuat &k0, const KeyframeQuat &k1, float t,
    glm::fquat *result) -> void {
    const auto &v0 = k0.value;
    const auto &v1 = k1.value;
    const auto t0 = k0.time;
    const auto t1 = k1.time;
    float l = (t1 - t0);
    float _t = (glm::clamp(t, t0, t1) - t0) / l;

    switch (mode) {
    case Model::Animation::InterpolationMode::eLinear:
        *result = glm::slerp(v0, v1, _t);
        break;
    case Model::Animation::InterpolationMode::eCubic:
    case Model::Animation::InterpolationMode::eStep:
        *result = v1;
        break;
    }
}

auto Model::Controller::updateAnimationChannel(uint32_t channel_id, const TranslationChannel &channel) -> void {
    updateAnimationChannel(
        translation_data_, channel_id, channel,
        [&](const KeyframeVec3 &k1, const KeyframeVec3 &k2, float t, Pose::Node &node) {
        glm::fvec3 result = node.translation();
        interpolate(channel.interpolation(), k1, k2, t, &result);

        node.setTranslationSilent(result);
    });
}

auto Model::Controller::updateAnimationChannel(uint32_t channel_id, const RotationChhannel &channel) -> void {
    updateAnimationChannel(
        rotation_data_, channel_id, channel,
        [&](const KeyframeQuat &k1, const KeyframeQuat &k2, float t, Pose::Node &node) {
        glm::fquat result = node.rotation();
        interpolate(channel.interpolation(), k1, k2, t, &result);

        node.setRotationSilent(result);
    });
}

auto Model::Controller::updateAnimationChannel(uint32_t channel_id, const ScaleChannel &channel) -> void {
    updateAnimationChannel(
        scale_data_, channel_id, channel,
        [&](const KeyframeVec3 &k1, const KeyframeVec3 &k2, float t, Pose::Node &node) {
        glm::fvec3 result = node.scale();
        interpolate(channel.interpolation(), k1, k2, t, &result);

        node.setScaleSilent(result);
    });
}

auto Model::Controller::updateAnimation(const Animation &animation) -> void {
    animation.iterateChannels<Animation::TargetProperty::eTranslation>(
        [&](uint32_t channel_id, const TranslationChannel &channel) { updateAnimationChannel(channel_id, channel); });

    animation.iterateChannels<Animation::TargetProperty::eRotation>(
        [&](uint32_t channel_id, const RotationChhannel &channel) { updateAnimationChannel(channel_id, channel); });

    animation.iterateChannels<Animation::TargetProperty::eScale>(
        [&](uint32_t channel_id, const ScaleChannel &channel) { updateAnimationChannel(channel_id, channel); });

    pose_->recomputeTransformSubtree(pose_->root());
}

inline auto convertInterpolation(act::AnimationInterpolationMode mode) -> Model::Animation::InterpolationMode {
    switch (mode) {
    case act::AnimationInterpolationMode::eStep:
        return Model::Animation::InterpolationMode::eStep;
    case act::AnimationInterpolationMode::eLinear:
        return Model::Animation::InterpolationMode::eLinear;
    case act::AnimationInterpolationMode::eCubicSpline:
        return Model::Animation::InterpolationMode::eCubic;
    default:
        return Model::Animation::InterpolationMode::eStep;
    }
}

auto Model::createController(AnimationId animation) -> Controller { return Controller{this, animation}; }

auto Model::fromAct(Renderer *renderer, const act::Model &act_model) -> std::unique_ptr<Model> {
    std::unique_ptr<Model> model{new (std::nothrow) Model(renderer)};
    if (!model) {
        return nullptr;
    }

    const auto num_nodes = act_model.nodes.size();
    const auto num_textures = act_model.textures.size();
    const auto num_materials = act_model.materials.size();
    const auto num_submeshes = act_model.submeshes.size();

    using AnyMeshId = std::variant<MeshId, AnimatedMeshId, std::monostate>;

    std::vector<std::optional<NodeId>> node_map;
    std::vector<std::optional<TextureId>> texture_map;
    std::vector<std::optional<MaterialId>> material_map;
    std::vector<AnyMeshId> submesh_map;

    node_map.resize(num_nodes);
    texture_map.resize(num_textures);
    material_map.resize(num_materials);
    submesh_map.resize(num_submeshes, std::monostate{});

    for (uint32_t act_texture_id = 0; act_texture_id < num_textures; ++act_texture_id) {
        const auto &act_texture = act_model.textures[act_texture_id];
        const auto &act_image = act_model.images[act_texture.image_id];

        std::vector<uint8_t> bitmap_buffer;

        const uint8_t *input_buffer_ptr = act_image.buffer.data();
        const auto input_buffer_size = static_cast<int>(act_image.buffer.size());
        int width = 0, height = 0, components = 0;

        int res = stbi_info_from_memory(input_buffer_ptr, input_buffer_size, &width, &height, &components);
        if (0 == res) {
            LogError("model: act image {} is invalid", act_texture.image_id);
            return nullptr;
        }

        auto img = stbi_load_from_memory(input_buffer_ptr, input_buffer_size, &width, &height, &components, 4);
        bitmap_buffer.resize(width * height * 4);

        ::memcpy(bitmap_buffer.data(), img, width * height * 4 * sizeof(uint8_t));
        stbi_image_free(img);

        auto texture_id =
            model->addRgbaTexture(static_cast<uint32_t>(width), static_cast<uint32_t>(height), bitmap_buffer);
        texture_map[act_texture_id] = texture_id;
    }

    for (uint32_t act_material_id = 0; act_material_id < num_materials; ++act_material_id) {
        const auto &act_material = act_model.materials[act_material_id];
        Material::Description desc;

        desc.diffuse =
            act_material.albedo_map_id.has_value() ? texture_map[act_material.albedo_map_id.value()] : std::nullopt;
        desc.normal =
            act_material.normal_map_id.has_value() ? texture_map[act_material.normal_map_id.value()] : std::nullopt;

        auto material_id = model->addMaterial(desc);
        material_map[act_material_id] = material_id;
    }

    // node initialization
    std::queue<uint32_t> node_queue;
    for (uint32_t id = 0; id < num_nodes; ++id) {
        node_queue.push(id);
    }

    std::vector<int> nodes_with_meshes;

    for (uint32_t iter = 0; !node_queue.empty() && iter < num_nodes * num_nodes; ++iter) {
        auto act_node_id = node_queue.front();
        node_queue.pop();

        const auto &act_node = act_model.nodes[act_node_id];
        auto parent_id = model->root();

        if (act_node.parent_id.has_value()) {
            const auto act_parent_id = act_node.parent_id.value();
            if (!node_map[act_parent_id].has_value()) {
                node_queue.push(act_node_id);
                continue;
            }

            parent_id = node_map[act_parent_id].value();
        }

        const auto node_id = model->addNode(parent_id, fmt::format("act_node_{}", act_node_id));
        if (!node_id.has_value()) {
            LogError("model: failed to allocate node");
            return nullptr;
        }

        node_map[act_node_id] = node_id;
        auto node = model->getNode(node_id.value());
        if (!node) {
            LogError("model: fatal, invalid id");
            return nullptr;
        }

        node->setTranslation(act_node.translation);
        node->setRotation(act_node.rotation);
        node->setScale(act_node.scale);

        if (act_node.mesh_id.has_value()) {
            nodes_with_meshes.push_back(act_node_id);
        }
    }

    if (!node_queue.empty()) {
        LogError("model: act model has invalid topology");
    }

    for (const auto act_node_id : nodes_with_meshes) {
        const auto &act_node = act_model.nodes[act_node_id];
        const auto &act_mesh = act_model.meshes[act_node.mesh_id.value()];

        auto node_id = node_map[act_node_id];
        auto &node = model->nodes_[node_id->index()];

        std::optional<SkinId> skin_id = [&]() -> std::optional<SkinId> {
            if (!act_node.skin_id.has_value()) {
                return std::nullopt;
            }

            const auto &act_skin = act_model.skins[act_node.skin_id.value()];

            Skin skin;
            for (const auto &act_skin_node_id : act_skin.skin_node_ids) {
                const auto &act_skin_node = act_model.skin_nodes[act_skin_node_id];
                if (!node_map[act_skin_node.node_id].has_value()) {
                    return std::nullopt;
                }

                skin.addNodeRef(node_map[act_skin_node.node_id].value(), act_skin_node.inverse_bind_matrix);
            }

            return model->addSkin(std::move(skin));
        }();

        for (const auto act_submesh_id : act_mesh.submesh_ids) {
            const auto &act_any_submesh = act_model.submeshes[act_submesh_id];
            std::visit(
                util::overload{
                    [&](const act::Model::StaticSubmesh &act_submesh) {
                std::vector<graphics::Renderer::StaticVertex> vertices{act_submesh.vertices.size()};
                for (size_t i = 0; i < act_submesh.vertices.size(); ++i) {
                    vertices[i].position = glm::fvec4{act_submesh.vertices[i].position, 0.0f};
                    vertices[i].normal = glm::fvec4{act_submesh.vertices[i].normal, 0.0f};
                    vertices[i].tangent = act_submesh.vertices[i].tangent;
                    vertices[i].uv = glm::fvec4{act_submesh.vertices[i].texcoord, 0.0f, 0.0f};
                }

                auto material_id = material_map[act_submesh.material];
                if (!material_id.has_value()) {
                    LogError("model: invalid material for act submesh {}", act_submesh_id);
                    return;
                }

                auto mesh_id = model->addMesh(vertices, act_submesh.indices, material_id.value());
                submesh_map[act_submesh_id] = mesh_id.has_value() ? AnyMeshId{mesh_id.value()} : std::monostate{};

                if (mesh_id.has_value()) {
                    node.addMesh(mesh_id.value());
                }
            },
                    [&](const act::Model::RiggedSubmesh &act_submesh) {
                // don't add animated submesh if skin is not present
                if (!skin_id.has_value()) {
                    return;
                }

                std::vector<graphics::Renderer::SkinnedVertex> vertices{act_submesh.vertices.size()};
                for (size_t i = 0; i < act_submesh.vertices.size(); ++i) {
                    vertices[i].position = glm::fvec4{act_submesh.vertices[i].position, 0.0f};
                    vertices[i].normal = glm::fvec4{act_submesh.vertices[i].normal, 0.0f};
                    vertices[i].tangent = act_submesh.vertices[i].tangent;
                    vertices[i].uv = glm::fvec4{act_submesh.vertices[i].texcoord, 0.0f, 0.0f};
                    vertices[i].bones = act_submesh.vertices[i].joints;
                    vertices[i].weights = act_submesh.vertices[i].weights;
                }

                auto material_id = material_map[act_submesh.material];
                if (!material_id.has_value()) {
                    LogError("model: invalid material for act submesh {}", act_submesh_id);
                    return;
                }

                auto mesh_id =
                    model->addAnimatedMesh(vertices, act_submesh.indices, material_id.value(), skin_id.value());
                submesh_map[act_submesh_id] = mesh_id.has_value() ? AnyMeshId{mesh_id.value()} : std::monostate{};

                if (mesh_id.has_value()) {
                    node.addAnimMesh(mesh_id.value());
                }
            },
                },
                act_any_submesh);
        }
    }

    // loading animations
    for (uint32_t act_anim_id = 0; act_anim_id < act_model.animations.size(); ++act_anim_id) {
        const auto &act_anim = act_model.animations[act_anim_id];

        Model::Animation animation{fmt::format("act_anim_{}", act_anim_id)};
        for (const auto act_channel_id : act_anim.channel_ids) {
            const auto &act_channel = act_model.animation_channels[act_channel_id];
            std::visit(
                util::overload{
                    [&](const act::Model::TranslationAnimationChannel &act_channel) {
                if (!node_map[act_channel.node_id].has_value()) {
                    return;
                }

                const auto node_id = node_map[act_channel.node_id].value();
                Animation::TranslationChannel channel{node_id};

                channel.setInterpolation(convertInterpolation(act_channel.interpolation));

                for (const auto &act_keyframe : act_channel.keyframes) {
                    channel.keyframes().emplace_back(
                        Animation::TranslationChannel::KeyframeType{
                            .value = act_keyframe.value,
                            .time = act_keyframe.time,
                        });
                }

                animation.appendChannel(std::move(channel));
            },
                    [&](const act::Model::RotationAnimationChannel &act_channel) {
                if (!node_map[act_channel.node_id].has_value()) {
                    return;
                }

                const auto node_id = node_map[act_channel.node_id].value();
                Animation::RotationChannel channel{node_id};

                channel.setInterpolation(convertInterpolation(act_channel.interpolation));

                for (const auto &act_keyframe : act_channel.keyframes) {
                    channel.keyframes().emplace_back(
                        Animation::RotationChannel::KeyframeType{
                            .value = act_keyframe.value,
                            .time = act_keyframe.time,
                        });
                }

                animation.appendChannel(std::move(channel));
            },
                    [&](const act::Model::ScaleAnimationChannel &act_channel) {
                if (!node_map[act_channel.node_id].has_value()) {
                    return;
                }

                const auto node_id = node_map[act_channel.node_id].value();
                Animation::ScaleChannel channel{node_id};

                channel.setInterpolation(convertInterpolation(act_channel.interpolation));

                for (const auto &act_keyframe : act_channel.keyframes) {
                    channel.keyframes().emplace_back(
                        Animation::ScaleChannel::KeyframeType{
                            .value = act_keyframe.value,
                            .time = act_keyframe.time,
                        });
                }

                animation.appendChannel(std::move(channel));
            },
                },
                act_channel);
        }

        model->addAnimation(std::move(animation));
    }

    return model;
}

} // namespace graphics
