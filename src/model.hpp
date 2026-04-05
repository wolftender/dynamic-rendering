#pragma once
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "act.hpp"
#include "graphics/renderer.hpp"

namespace graphics {

template <typename T>
concept AnimationPropertyType = std::convertible_to<T, glm::fvec3> || std::convertible_to<T, glm::fquat>;

class Model final {
    struct MeshTag {};
    struct NodeTag {};
    struct TextureTag {};
    struct MaterialTag {};
    struct SkinTag {};
    struct AnimatedMeshTag {};
    struct AnimationTag {};

    template <typename T> static auto getResource(Renderer *renderer, T handle) -> const T::Resource *;
    template <typename T> static auto deleteResource(Renderer *renderer, T handle) -> void;

    template <typename T> class RendererResource final {
    public:
        RendererResource(Renderer *renderer, T handle) : renderer_{renderer}, handle_{handle} {}
        ~RendererResource() noexcept {
            if (handle_.has_value() && renderer_) {
                Model::deleteResource(renderer_, handle_.value());
            }
        }

        RendererResource(const RendererResource &) = delete;
        auto operator=(const RendererResource &) = delete;

        RendererResource(RendererResource &&r) noexcept {
            renderer_ = r.renderer_;
            handle_ = std::move(r.handle_);

            r.renderer_ = nullptr;
            r.handle_ = std::nullopt;
        }

        auto operator=(RendererResource &&r) noexcept -> RendererResource & {
            if (this != &r) {
                renderer_ = r.renderer_;
                handle_ = std::move(r.handle_);

                r.renderer_ = nullptr;
                r.handle_ = std::nullopt;
            }

            return *this;
        }

        auto get() const -> const T * {
            if (handle_.has_value()) {
                return Model::getResource(renderer_, handle_.value());
            }

            return nullptr;
        }

        auto id() const -> T { return handle_.value(); }

    private:
        Renderer *renderer_ = nullptr;
        std::optional<T> handle_;
    };

public:
    template <typename Tag> class Id {
    public:
        auto index() const -> uint32_t { return index_; }

        Id(const Id &) = default;
        auto operator=(const Id &) -> Id & = default;

    private:
        explicit Id(uint32_t index) : index_{index} {}
        uint32_t index_;

        friend class Node;
        friend class Model;
        friend class Pose;
    };

    using MeshId = Id<MeshTag>;
    using NodeId = Id<NodeTag>;
    using TextureId = Id<TextureTag>;
    using MaterialId = Id<MaterialTag>;
    using SkinId = Id<SkinTag>;
    using AnimatedMeshId = Id<AnimatedMeshTag>;
    using AnimationId = Id<AnimationTag>;

    class Texture final {
    public:
        ~Texture() noexcept = default;

        Texture(const Texture &) = delete;
        auto operator=(const Texture &) = delete;

        Texture(Texture &&t) noexcept = default;
        auto operator=(Texture &&t) noexcept -> Texture & = default;

        auto model() const -> const Model & { return *model_; }
        auto handle() const -> const RendererResource<Renderer::TextureId> & { return handle_; }

    private:
        Texture(Model *model, RendererResource<Renderer::TextureId> handle)
            : model_{model}, handle_{std::move(handle)} {}

        Model *model_ = nullptr;
        RendererResource<Renderer::TextureId> handle_;

        friend class Model;
    };

    class Mesh final {
    public:
        ~Mesh() noexcept = default;

        Mesh(const Mesh &) = delete;
        auto operator=(const Mesh &) = delete;

        Mesh(Mesh &&t) noexcept = default;
        auto operator=(Mesh &&t) noexcept -> Mesh & = default;

        auto model() const -> const Model & { return *model_; }
        auto handle() const -> const RendererResource<Renderer::MeshId> & { return handle_; }
        auto material() const -> MaterialId { return material_; }

    private:
        Mesh(Model *model, RendererResource<Renderer::MeshId> handle, MaterialId material)
            : model_{model}, handle_{std::move(handle)}, material_{material} {}

        Model *model_ = nullptr;
        RendererResource<Renderer::MeshId> handle_;
        MaterialId material_;

        friend class Model;
    };

    class Skin final {
    public:
        struct SkinNode {
            NodeId node;
            glm::fmat4x4 inverse_bind;
        };

        Skin() = default;

        template <util::TypedContiguousRange<SkinNode> R>
        Skin(R &nodes) : nodes_{std::ranges::begin(nodes), std::ranges::end(nodes)} {}

        auto addNodeRef(NodeId node, const glm::fmat4x4 &inverse_bind) -> void {
            nodes_.push_back({node, inverse_bind});
        }

        auto addNodeRef(SkinNode node) -> void { nodes_.emplace_back(node); }
        auto nodes() const -> const std::vector<SkinNode> & { return nodes_; }

    private:
        std::vector<SkinNode> nodes_;
    };

    class AnimatedMesh final {
    public:
        ~AnimatedMesh() noexcept = default;

        AnimatedMesh(const AnimatedMesh &) = delete;
        auto operator=(const AnimatedMesh &) = delete;

        AnimatedMesh(AnimatedMesh &&t) noexcept = default;
        auto operator=(AnimatedMesh &&t) noexcept -> AnimatedMesh & = default;

        auto model() const -> const Model & { return *model_; }
        auto handle() const -> const RendererResource<Renderer::AnimatedMeshId> & { return handle_; }
        auto material() const -> MaterialId { return material_; }
        auto skin() const -> SkinId { return skin_; }

    private:
        AnimatedMesh(Model *model, RendererResource<Renderer::AnimatedMeshId> handle, MaterialId material, SkinId skin)
            : model_{model}, handle_{std::move(handle)}, material_{material}, skin_{std::move(skin)} {}

        Model *model_ = nullptr;
        RendererResource<Renderer::AnimatedMeshId> handle_;
        MaterialId material_;
        SkinId skin_;

        friend class Model;
    };

    class Node final {
    public:
        Node(const Node &) = delete;
        auto operator=(const Node &) = delete;

        Node(Node &&) noexcept = default;
        auto operator=(Node &&) noexcept -> Node & = default;

        auto model() const -> const Model & { return *model_; }
        auto self() const -> NodeId { return self_; }
        auto parent() const -> const std::optional<NodeId> { return parent_; }
        auto translation() const -> const glm::fvec3 & { return translation_; }
        auto scale() const -> const glm::fvec3 & { return scale_; }
        auto rotation() const -> const glm::fquat & { return rotation_; }
        auto children() const -> const std::vector<NodeId> & { return children_; }
        auto meshes() const -> const std::vector<MeshId> & { return meshes_; }
        auto animatedMeshes() const -> const std::vector<AnimatedMeshId> & { return animated_meshes_; }

        auto setTranslation(const glm::fvec3 &translation) -> void { translation_ = translation; }
        auto setScale(const glm::fvec3 &scale) -> void { scale_ = scale; }
        auto setRotation(const glm::fquat &rotation) -> void { rotation_ = rotation; }

        auto addMesh(MeshId id) -> void;
        auto addAnimMesh(AnimatedMeshId id) -> void;

    private:
        Node(
            Model *model, NodeId self, std::string_view name, std::optional<NodeId> parent,
            const glm::fvec3 &translation, const glm::fvec3 &scale, const glm::fquat &rotation)
            : model_{model}, self_{self}, name_{name}, parent_{parent}, translation_{translation}, scale_{scale},
              rotation_{rotation} {}

        Model *model_ = nullptr;
        NodeId self_;
        std::string name_;
        std::optional<NodeId> parent_;
        glm::fvec3 translation_;
        glm::fvec3 scale_;
        glm::fquat rotation_;
        std::vector<MeshId> meshes_;
        std::vector<AnimatedMeshId> animated_meshes_;
        std::vector<NodeId> children_;

        friend class Model;
    };

    class Material final {
    public:
        struct Description {
            std::optional<TextureId> diffuse;
            std::optional<TextureId> normal;
            std::optional<TextureId> specular;
        };

        Material(const Material &) = delete;
        auto operator=(const Material &) = delete;

        Material(Material &&) noexcept = default;
        auto operator=(Material &&) noexcept -> Material & = default;

        auto model() const -> const Model & { return *model_; }
        auto diffuse() const -> std::optional<TextureId> { return diffuse_; }
        auto normal() const -> std::optional<TextureId> { return normal_; }
        auto specular() const -> std::optional<TextureId> { return specular_; }

    private:
        Material(
            Model *model, std::optional<TextureId> diffuse, std::optional<TextureId> normal,
            std::optional<TextureId> specular)
            : model_{model}, diffuse_{diffuse}, normal_{normal}, specular_{specular} {}

        Model *model_ = nullptr;
        std::optional<TextureId> diffuse_;
        std::optional<TextureId> normal_;
        std::optional<TextureId> specular_;

        friend class Model;
    };

    class Animation final {
    public:
        enum class InterpolationMode {
            eStep,
            eLinear,
            eCubic,
        };

        enum class TargetProperty {
            eRotation,
            eTranslation,
            eScale,
        };

        template <TargetProperty Prop> struct PropertyDataType;

        template <> struct PropertyDataType<TargetProperty::eRotation> {
            using Type = glm::fquat;
        };

        template <> struct PropertyDataType<TargetProperty::eTranslation> {
            using Type = glm::fvec3;
        };

        template <> struct PropertyDataType<TargetProperty::eScale> {
            using Type = glm::fvec3;
        };

        template <AnimationPropertyType T> struct Keyframe final {
            T value;
            float time;
        };

        template <TargetProperty Prop> class Channel final {
        public:
            using PropertyType = typename PropertyDataType<Prop>::Type;
            using KeyframeType = Keyframe<PropertyType>;

            explicit Channel(NodeId node) : node_{node}, interpolation_{InterpolationMode::eLinear} {}

            ~Channel() noexcept = default;

            Channel(const Channel &) = delete;
            auto operator=(const Channel &) = delete;

            Channel(Channel &&) noexcept = default;
            auto operator=(Channel &&) noexcept -> Channel & = default;

            auto keyframes() const -> const std::vector<KeyframeType> & { return keyframes_; }
            auto interpolation() const -> InterpolationMode { return interpolation_; }
            auto node() const -> NodeId { return node_; }

            auto keyframes() -> std::vector<KeyframeType> & { return keyframes_; }
            auto setInterpolation(InterpolationMode mode) { interpolation_ = mode; }

            auto sort() -> void {
                std::sort(
                    keyframes_.begin(), keyframes_.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
            }

        private:
            std::vector<KeyframeType> keyframes_;

            NodeId node_;
            InterpolationMode interpolation_;
        };

        using TranslationChannel = Channel<TargetProperty::eTranslation>;
        using RotationChannel = Channel<TargetProperty::eRotation>;
        using ScaleChannel = Channel<TargetProperty::eScale>;

        using AnyChannel = std::variant<RotationChannel, TranslationChannel, ScaleChannel>;

        explicit Animation(std::string_view name) : duration_{0.0f}, name_{name} {}

        Animation(const Animation &) = delete;
        auto operator=(const Animation &) = delete;

        Animation(Animation &&) noexcept = default;
        auto operator=(Animation &&) noexcept -> Animation & = default;

        auto duration() const -> float { return duration_; }
        auto name() const -> const std::string & { return name_; }

        auto numTranslationChannels() const -> uint32_t { return translation_channels_.size(); }
        auto numRotationChannels() const -> uint32_t { return rotation_channels_.size(); }
        auto numScaleChannels() const -> uint32_t { return scale_channels_.size(); }

        template <TargetProperty Prop> auto appendChannel(Channel<Prop> &&channel) -> void {
            const auto last_keyframe_time = !channel.keyframes().empty() ? channel.keyframes().back().time : 0.0f;
            duration_ = std::max(duration_, last_keyframe_time);

            if constexpr (Prop == TargetProperty::eTranslation) {
                translation_channels_.emplace_back(std::move(channel));
            } else if constexpr (Prop == TargetProperty::eRotation) {
                rotation_channels_.emplace_back(std::move(channel));
            } else if constexpr (Prop == TargetProperty::eScale) {
                scale_channels_.emplace_back(std::move(channel));
            } else {
                static_assert(false, "unsupported channel type");
            }
        }

        template <TargetProperty Prop, std::invocable<uint32_t, const Channel<Prop> &> F>
        auto iterateChannels(F consumer) const -> void {
            auto &channels = [this]() -> const auto & {
                if constexpr (Prop == TargetProperty::eTranslation) {
                    return translation_channels_;
                } else if constexpr (Prop == TargetProperty::eRotation) {
                    return rotation_channels_;
                } else if constexpr (Prop == TargetProperty::eScale) {
                    return scale_channels_;
                }
            }();

            for (uint32_t channel_id = 0; channel_id < channels.size(); ++channel_id) {
                consumer(channel_id, channels[channel_id]);
            }
        }

    private:
        std::vector<TranslationChannel> translation_channels_;
        std::vector<RotationChannel> rotation_channels_;
        std::vector<ScaleChannel> scale_channels_;

        float duration_;
        std::string name_;
    };

    class Pose final {
    public:
        class Node final {
        public:
            Node(const Node &) = delete;
            auto operator=(const Node &) = delete;

            Node(Node &&) noexcept = default;
            auto operator=(Node &&) noexcept -> Node & = default;

            auto pose() -> Pose & { return *pose_; }
            auto pose() const -> const Pose & { return *pose_; }

            auto self() const -> NodeId { return self_; }
            auto transform() const -> const glm::fmat4x4 & { return transform_; }

            auto setTranslation(const glm::fvec3 &translation) -> void;
            auto setScale(const glm::fvec3 &scale) -> void;
            auto setRotation(const glm::fquat &rotation) -> void;

            auto setTranslationSilent(const glm::fvec3 &translation) -> void;
            auto setScaleSilent(const glm::fvec3 &scale) -> void;
            auto setRotationSilent(const glm::fquat &rotation) -> void;

            auto translation() const -> const glm::fvec3 & { return translation_; }
            auto scale() const -> const glm::fvec3 & { return scale_; }
            auto rotation() const -> const glm::fquat & { return rotation_; }

            auto setTransform(const glm::fvec3 &translation, const glm::fvec3 &scale, const glm::fquat &rotation)
                -> void;

        private:
            Node(Pose *pose, NodeId node) : pose_{pose}, self_{node} {};

            Pose *pose_;
            NodeId self_;
            std::optional<NodeId> parent_;

            glm::fvec3 translation_;
            glm::fvec3 scale_;
            glm::fquat rotation_;
            glm::fmat4x4 transform_;

            std::vector<NodeId> children_;

            friend class Model;
            friend class Pose;
        };

        ~Pose() noexcept;

        Pose(const Pose &) = delete;
        auto operator=(const Pose &) = delete;

        Pose(Pose &&) noexcept = delete;
        auto operator=(Pose &&) noexcept -> Pose & = delete;

        auto root() const -> NodeId { return NodeId{0ul}; }
        auto getNode(NodeId handle) -> Node *;
        auto getNode(NodeId handle) const -> const Node *;

        template <std::invocable<const Node &> F> auto withNode(NodeId handle, F consumer) const -> void {
            const auto node = getNode(handle);
            if (node) {
                consumer(*node);
            }
        }

        template <std::invocable<Node &> F> auto withNodeMut(NodeId handle, F consumer) -> void {
            auto node = getNode(handle);
            if (node) {
                consumer(*node);
            }
        }

        template <std::invocable<NodeId, glm::fvec3 &, glm::fvec3 &, glm::fquat &> Curve>
        auto applyCurve(Curve curve) -> void {
            for (auto &node : nodes_) {
                curve(node.self_, node.translation_, node.scale_, node.rotation_);
            }

            recomputeTransformSubtree(root());
        }

    private:
        static auto fromModel(const Model &model) -> std::unique_ptr<Pose>;

        Pose() = default;

        auto recomputeTransformSubtree(NodeId root) -> void;

        Renderer *renderer_ = nullptr;

        std::vector<Node> nodes_;
        std::vector<Renderer::ActorMeshId> actor_meshes_;

        friend class Model;
        friend class Controller;
    };

    Model(Renderer *renderer);
    ~Model() noexcept = default;

    Model(const Model &) = delete;
    auto operator=(const Model &) = delete;

    Model(Model &&) noexcept = delete;
    auto operator=(Model &&) noexcept = delete;

    auto renderer() -> Renderer & { return *renderer_; }
    auto renderer() const -> const Renderer & { return *renderer_; }

    auto createPose() const -> std::unique_ptr<Pose>;
    auto root() const -> NodeId { return NodeId{0ul}; }

    template <util::TypedContiguousRange<Renderer::StaticVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto addMesh(const VR &vertices, const IR &indices, MaterialId material) -> std::optional<MeshId> {
        const auto vtx_ptr = std::ranges::data(vertices);
        const auto idx_ptr = std::ranges::data(indices);

        const auto vtx_len = std::ranges::size(vertices);
        const auto idx_len = std::ranges::size(indices);

        return addMeshImpl({vtx_ptr, vtx_len}, {idx_ptr, idx_len}, material);
    }

    template <util::TypedContiguousRange<Renderer::SkinnedVertex> VR, util::TypedContiguousRange<const uint32_t> IR>
    auto addAnimatedMesh(const VR &vertices, const IR &indices, MaterialId material, SkinId skin)
        -> std::optional<AnimatedMeshId> {
        const auto vtx_ptr = std::ranges::data(vertices);
        const auto idx_ptr = std::ranges::data(indices);

        const auto vtx_len = std::ranges::size(vertices);
        const auto idx_len = std::ranges::size(indices);

        return addAnimMeshImpl({vtx_ptr, vtx_len}, {idx_ptr, idx_len}, material, skin);
    }

    template <util::TypedContiguousRange<const uint8_t> R>
    auto addRgbaTexture(uint32_t width, uint32_t height, const R &range) -> std::optional<TextureId> {
        const auto data_ptr = std::ranges::data(range);
        const auto data_len = std::ranges::size(range);

        return addRgbaTextureImpl(width, height, {data_ptr, data_len});
    }

    auto addSkin(Skin &&skin) -> std::optional<SkinId>;

    auto addMaterial(const Material::Description &desc) -> std::optional<MaterialId>;
    auto addNode(NodeId parent, std::string_view name) -> std::optional<NodeId>;
    auto addAnimation(Animation &&animation) -> std::optional<AnimationId>;

    auto getMesh(MeshId handle) -> Mesh *;
    auto getAnimMesh(AnimatedMeshId handle) -> AnimatedMesh *;
    auto getNode(NodeId handle) -> Node *;
    auto getTexture(TextureId handle) -> Texture *;
    auto getMaterial(MaterialId handle) -> Material *;
    auto getAnimation(AnimationId handle) -> Animation *;

    auto getMesh(MeshId handle) const -> const Mesh *;
    auto getAnimMesh(AnimatedMeshId handle) const -> const AnimatedMesh *;
    auto getNode(NodeId handle) const -> const Node *;
    auto getTexture(TextureId handle) const -> const Texture *;
    auto getMaterial(MaterialId handle) const -> const Material *;
    auto getAnimation(AnimationId handle) const -> const Animation *;

    template <std::invocable<const Mesh &> F> auto withMesh(MeshId handle, F consumer) const -> void {
        const auto mesh = getMesh(handle);
        if (mesh) {
            consumer(*mesh);
        }
    }

    template <std::invocable<Mesh &> F> auto withMeshMut(MeshId handle, F consumer) -> void {
        auto mesh = getMesh(handle);
        if (mesh) {
            consumer(*mesh);
        }
    }

    template <std::invocable<const AnimatedMesh &> F>
    auto withAnimMesh(AnimatedMeshId handle, F consumer) const -> void {
        auto mesh = getAnimMesh(handle);
        if (mesh) {
            consumer(*mesh);
        }
    }

    template <std::invocable<AnimatedMesh &> F> auto withAnimMeshMut(AnimatedMeshId handle, F consumer) -> void {
        auto mesh = getAnimMesh(handle);
        if (mesh) {
            consumer(*mesh);
        }
    }

    template <std::invocable<const Node &> F> auto withNode(NodeId handle, F consumer) const -> void {
        const auto node = getNode(handle);
        if (node) {
            consumer(*node);
        }
    }

    template <std::invocable<Node &> F> auto withNodeMut(NodeId handle, F consumer) -> void {
        auto node = getNode(handle);
        if (node) {
            consumer(*node);
        }
    }

    template <std::invocable<const Texture &> F> auto withTexture(TextureId handle, F consumer) const -> void {
        const auto texture = getTexture(handle);
        if (texture) {
            consumer(*texture);
        }
    }

    template <std::invocable<Texture &> F> auto withTextureMut(TextureId handle, F consumer) -> void {
        auto texture = getTexture(handle);
        if (texture) {
            consumer(*texture);
        }
    }

    template <std::invocable<const Material &> F> auto withMaterial(MaterialId handle, F consumer) const -> void {
        const auto material = getMaterial(handle);
        if (material) {
            consumer(*material);
        }
    }

    template <std::invocable<Material &> F> auto withMaterialMut(MaterialId handle, F consumer) -> void {
        auto material = getMaterial(handle);
        if (material) {
            consumer(*material);
        }
    }

    template <std::invocable<const Animation &> F> auto withAnimation(AnimationId handle, F consumer) const -> void {
        const auto animation = getAnimation(handle);
        if (animation) {
            consumer(*animation);
        }
    }

    std::vector<AnimationId> makeAnimationList() const {
        std::vector<AnimationId> animations;
        for (uint32_t anim_id = 0; anim_id < animations_.size(); ++anim_id) {
            animations.push_back(AnimationId{anim_id});
        }

        return animations;
    }

    template <std::invocable<Animation &> F> auto withAnimationMut(AnimationId handle, F consumer) -> void {
        auto animation = getAnimation(handle);
        if (animation) {
            consumer(*animation);
        }
    }

    template <std::invocable<NodeId, const Node &> F> auto iterateNodes(F consumer) const -> void {
        for (uint32_t id = 0; id < nodes_.size(); ++id) {
            consumer(NodeId{id}, nodes_[id]);
        }
    }

    template <std::invocable<NodeId, const Node &> F> auto iterateMeshNodes(F consumer) const -> void {
        for (const auto &id : mesh_nodes_) {
            consumer(id, nodes_[id.index()]);
        }
    }

    auto render(Renderer &renderer, const Pose &pose, const glm::fmat4x4 &world) const -> void;

    static auto fromAct(Renderer *renderer, const act::Model &act_model) -> std::unique_ptr<Model>;

    class Controller final {
    private:
        struct ChannelData {
            uint32_t prev_keyframe;
            uint32_t next_keyframe;

            float prev_keyframe_time;
            float next_keyframe_time;
        };

        Controller(const Model *model, AnimationId animation)
            : model_{model}, animation_{animation}, time_{0.0f}, loop_{true} {
            pose_ = model->createPose();
            initializeAnimationData();
        }

        using TranslationChannel = Animation::TranslationChannel;
        using RotationChhannel = Animation::RotationChannel;
        using ScaleChannel = Animation::ScaleChannel;

    public:
        Controller(const Controller &) = delete;
        auto operator=(const Controller &) -> Controller & = delete;

        Controller(Controller &&) noexcept = default;
        auto operator=(Controller &&) noexcept -> Controller & = default;

        ~Controller() = default;

        auto animation() const -> AnimationId { return animation_; }
        auto loop() const -> bool { return loop_; }
        auto time() const -> float { return time_; }
        auto pose() const -> const Pose & { return *pose_.get(); }

        auto setLoop(bool loop) -> void { loop_ = loop; }

        auto setAnimation(AnimationId id) -> void;
        auto integrate(float delta_time) -> void;
        auto seek(float time) -> void;

    private:
        template <Animation::TargetProperty Prop>
        auto resetChannelData(
            std::vector<ChannelData> &channel_data, uint32_t channel_id, const Animation::Channel<Prop> &channel)
            -> void {
            const auto &keyframes = channel.keyframes();

            if (keyframes.size() == 0) { // invalid channel dont initialize?
                return;
            }

            if (keyframes.size() == 1) { // one keyframe, simply add it
                channel_data[channel_id].prev_keyframe = 0;
                channel_data[channel_id].next_keyframe = 0;
                channel_data[channel_id].prev_keyframe_time = 0.0f;
                channel_data[channel_id].next_keyframe_time = 99999.0f;
                return;
            }

            // otherwise pick the first keyframe and the second keyframe
            channel_data[channel_id].prev_keyframe = 0;
            channel_data[channel_id].next_keyframe = 1;
            channel_data[channel_id].prev_keyframe_time = keyframes[0].time;
            channel_data[channel_id].next_keyframe_time = keyframes[1].time;
        }

        auto resetAnimationData(const Animation &animation) -> void;
        auto initializeAnimationData() -> void;

        template <
            Animation::TargetProperty Prop,
            std::invocable<
                const typename Animation::Channel<Prop>::KeyframeType &,
                const typename Animation::Channel<Prop>::KeyframeType &, float, Pose::Node &>
                F>
        auto updateAnimationChannel(
            std::vector<ChannelData> &channel_data_list, uint32_t channel_id, const Animation::Channel<Prop> &channel,
            F interpolate) -> void {
            const auto &keyframes = channel.keyframes();
            auto &channel_data = channel_data_list[channel_id];

            if (time_ > channel_data.next_keyframe_time) {
                const auto num_keyframes = keyframes.size();
                do {
                    channel_data.prev_keyframe++;
                    channel_data.next_keyframe++;

                    channel_data.prev_keyframe_time = keyframes[channel_data.prev_keyframe].time;
                    channel_data.next_keyframe_time = keyframes[channel_data.next_keyframe].time;
                } while ((channel_data.next_keyframe < num_keyframes - 1) &&
                         (keyframes[channel_data.next_keyframe].time < time_));
            }

            const auto &prev_keyframe = keyframes[channel_data.prev_keyframe];
            const auto &next_keyframe = keyframes[channel_data.next_keyframe];

            pose_->withNodeMut(
                channel.node(), [&](auto &node) { interpolate(prev_keyframe, next_keyframe, time_, node); });
        }

        auto updateAnimationChannel(uint32_t channel_id, const TranslationChannel &channel) -> void;
        auto updateAnimationChannel(uint32_t channel_id, const RotationChhannel &channel) -> void;
        auto updateAnimationChannel(uint32_t channel_id, const ScaleChannel &channel) -> void;
        auto updateAnimation(const Animation &animation) -> void;

        const Model *model_;

        AnimationId animation_;
        std::unique_ptr<Pose> pose_;

        float time_;
        bool loop_;

        std::vector<ChannelData> translation_data_;
        std::vector<ChannelData> rotation_data_;
        std::vector<ChannelData> scale_data_;

        friend class Model;
    };

    auto createController(AnimationId animation) -> Controller;

private:
    auto addMeshImpl(
        std::span<const Renderer::StaticVertex> vertices, std::span<const uint32_t> indices, MaterialId material)
        -> std::optional<MeshId>;
    auto addAnimMeshImpl(
        std::span<const Renderer::SkinnedVertex> vertices, std::span<const uint32_t> indices, MaterialId material,
        SkinId skin) -> std::optional<AnimatedMeshId>;
    auto addRgbaTextureImpl(uint32_t width, uint32_t height, std::span<const uint8_t> data) -> std::optional<TextureId>;

    Renderer *renderer_;

    std::vector<Mesh> meshes_;
    std::vector<Node> nodes_;
    std::vector<Texture> textures_;
    std::vector<Material> materials_;
    std::vector<Skin> skins_;
    std::vector<AnimatedMesh> animated_meshes_;
    std::vector<NodeId> mesh_nodes_;
    std::vector<Animation> animations_;

    friend class Node;
};

} // namespace graphics
