#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "act.hpp"
#include "renderer.hpp"

namespace graphics {

class Model final {
    struct MeshTag {};
    struct NodeTag {};
    struct TextureTag {};
    struct MaterialTag {};

    template <typename T> static auto getResource(Renderer *renderer, Renderer::ResourceId<T> handle) -> const T *;
    template <typename T> static auto deleteResource(Renderer *renderer, Renderer::ResourceId<T> handle) -> void;

    template <typename T> class RendererResource final {
    public:
        RendererResource(Renderer *renderer, Renderer::ResourceId<T> handle) : renderer_{renderer}, handle_{handle} {}
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

    private:
        Renderer *renderer_ = nullptr;
        std::optional<Renderer::ResourceId<T>> handle_;
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

    class Texture final {
    public:
        ~Texture() noexcept = default;

        Texture(const Texture &) = delete;
        auto operator=(const Texture &) = delete;

        Texture(Texture &&t) noexcept = default;
        auto operator=(Texture &&t) noexcept -> Texture & = default;

        auto model() const -> const Model & { return *model_; }
        auto handle() const -> const RendererResource<Renderer::Texture> & { return handle_; }

    private:
        Texture(Model *model, RendererResource<Renderer::Texture> handle) : model_{model}, handle_{std::move(handle)} {}

        Model *model_ = nullptr;
        RendererResource<Renderer::Texture> handle_;

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
        auto handle() const -> const RendererResource<Renderer::Mesh> & { return handle_; }
        auto material() const -> MaterialId { return material_; }

    private:
        Mesh(Model *model, RendererResource<Renderer::Mesh> handle, MaterialId material)
            : model_{model}, handle_{std::move(handle)}, material_{material} {}

        Model *model_ = nullptr;
        RendererResource<Renderer::Mesh> handle_;
        MaterialId material_;

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

        auto setTranslation(const glm::fvec3 &translation) -> void { translation_ = translation; }
        auto setScale(const glm::fvec3 &scale) -> void { scale_ = scale; }
        auto setRotation(const glm::fquat &rotation) -> void { rotation_ = rotation; }

        auto addMesh(MeshId id) -> void;

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

        std::vector<Node> nodes_;
        friend class Model;
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

    template <util::TypedContiguousRange<const uint8_t> R>
    auto addRgbaTexture(uint32_t width, uint32_t height, const R &range) -> std::optional<TextureId> {
        const auto data_ptr = std::ranges::data(range);
        const auto data_len = std::ranges::size(range);

        return addRgbaTextureImpl(width, height, {data_ptr, data_len});
    }

    auto addMaterial(const Material::Description &desc) -> std::optional<MaterialId>;
    auto addNode(NodeId parent, std::string_view name) -> std::optional<NodeId>;

    auto getMesh(MeshId handle) -> Mesh *;
    auto getNode(NodeId handle) -> Node *;
    auto getTexture(TextureId handle) -> Texture *;
    auto getMaterial(MaterialId handle) -> Material *;

    auto getMesh(MeshId handle) const -> const Mesh *;
    auto getNode(NodeId handle) const -> const Node *;
    auto getTexture(TextureId handle) const -> const Texture *;
    auto getMaterial(MaterialId handle) const -> const Material *;

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

    template <std::invocable<NodeId, const Node &> F> auto iterateMeshNodes(F consumer) const -> void {
        for (const auto &id : mesh_nodes_) {
            consumer(nodes_[id.index()]);
        }
    }

    static auto fromAct(Renderer *renderer, const act::Model &act_model) -> std::unique_ptr<Model>;

private:
    auto addMeshImpl(
        std::span<const Renderer::StaticVertex> vertices, std::span<const uint32_t> indices, MaterialId material)
        -> std::optional<MeshId>;
    auto addRgbaTextureImpl(uint32_t width, uint32_t height, std::span<const uint8_t> data) -> std::optional<TextureId>;

    Renderer *renderer_;

    std::vector<Mesh> meshes_;
    std::vector<Node> nodes_;
    std::vector<Texture> textures_;
    std::vector<Material> materials_;
    std::vector<NodeId> mesh_nodes_;

    friend class Node;
};

} // namespace graphics
