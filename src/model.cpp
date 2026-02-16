#include <queue>
#include <stb_image/stb_image.h>
#include <glm/gtx/quaternion.hpp>

#include "logger.hpp"
#include "model.hpp"

namespace graphics {

template <>
auto Model::getResource<Renderer::Texture>(Renderer *renderer, Renderer::TextureId handle)
    -> const Renderer::Texture * {
    return renderer->getTexture(handle);
}

template <>
auto Model::getResource<Renderer::Mesh>(Renderer *renderer, Renderer::MeshId handle) -> const Renderer::Mesh * {
    return renderer->getMesh(handle);
}

template <> auto Model::deleteResource<Renderer::Texture>(Renderer *renderer, Renderer::TextureId handle) -> void {
    renderer->deleteTexture(handle);
}

template <> auto Model::deleteResource<Renderer::Mesh>(Renderer *renderer, Renderer::MeshId handle) -> void {
    renderer->deleteMesh(handle);
}

auto Model::Node::addMesh(MeshId id) -> void {
    const auto is_first_drawable = meshes_.size() == 0;
    meshes_.push_back(std::move(id));

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
    return pose;
}

auto Model::Pose::recomputeTransformSubtree(NodeId root) -> void {
    glm::fmat4x4 matrix{1.0f};

    auto node = getNode(root);
    if (!node) {
        return;
    }

    if (node->parent_.has_value()) {
        const auto parent = getNode(node->parent_.value());
        if (parent) {
            matrix = parent->transform();
        }
    }

    // T * R * S
    matrix = glm::translate(matrix, node->translation());
    matrix = glm::toMat4(node->rotation()) * matrix;
    matrix = glm::scale(matrix, node->scale_);
    node->transform_ = matrix;

    for (auto &child : node->children_) {
        recomputeTransformSubtree(child);
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

    meshes_.emplace_back(Mesh{this, RendererResource<Renderer::Mesh>{renderer_, mesh_rc.value()}, material});
    return MeshId{static_cast<uint32_t>(meshes_.size() - 1)};
}

auto Model::addRgbaTextureImpl(uint32_t width, uint32_t height, std::span<const uint8_t> data)
    -> std::optional<TextureId> {
    Renderer::Texture::Description desc = {
        .width = width,
        .height = height,
        .mag_filter = Renderer::Texture::MagFilter::eLinear,
        .min_filter = Renderer::Texture::MinFilter::eLinear,
    };

    auto texture_rc = renderer_->createRgbaTexture(desc, data);
    if (!texture_rc.has_value()) {
        return std::nullopt;
    }

    textures_.emplace_back(Texture{this, RendererResource<Renderer::Texture>{renderer_, texture_rc.value()}});
    return TextureId{static_cast<uint32_t>(textures_.size() - 1)};
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

auto Model::getMesh(MeshId handle) -> Mesh * {
    if (handle.index() >= meshes_.size()) {
        return nullptr;
    }

    return &meshes_[handle.index()];
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

auto Model::getMesh(MeshId handle) const -> const Mesh * {
    if (handle.index() >= meshes_.size()) {
        return nullptr;
    }

    return &meshes_[handle.index()];
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

            Renderer::OpaqueDrawDescription desc = {
                .mesh = mesh.handle().id(),
                .world_matrix = matrix,
            };

            renderer.drawOpaqueMesh(std::move(desc));
        }
    }
}

auto Model::fromAct(Renderer *renderer, const act::Model &act_model) -> std::unique_ptr<Model> {
    std::unique_ptr<Model> model{new (std::nothrow) Model(renderer)};
    if (!model) {
        return nullptr;
    }

    const auto num_nodes = act_model.nodes.size();
    const auto num_textures = act_model.textures.size();
    const auto num_materials = act_model.materials.size();
    const auto num_meshes = act_model.meshes.size();
    const auto num_submeshes = act_model.submeshes.size();

    std::vector<std::optional<NodeId>> node_map;
    std::vector<std::optional<TextureId>> texture_map;
    std::vector<std::optional<MaterialId>> material_map;
    std::vector<std::optional<MeshId>> submesh_map;

    node_map.resize(num_nodes);
    texture_map.resize(num_textures);
    material_map.resize(num_materials);
    submesh_map.resize(num_submeshes);

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

    for (uint32_t act_mesh_id = 0; act_mesh_id < num_meshes; ++act_mesh_id) {
        const auto &act_mesh = act_model.meshes[act_mesh_id];
        for (const auto &act_submesh_id : act_mesh.submesh_ids) {
            const auto &act_any_submesh = act_model.submeshes[act_submesh_id];
            std::visit(
                util::overload{
                    [&](const act::Model::StaticSubmesh &act_submesh) {
                std::vector<graphics::Renderer::StaticVertex> vertices{act_submesh.vertices.size()};
                for (size_t i = 0; i < act_submesh.vertices.size(); ++i) {
                    vertices[i].position = act_submesh.vertices[i].position;
                    vertices[i].normal = act_submesh.vertices[i].normal;
                    vertices[i].tangent = act_submesh.vertices[i].tangent;
                    vertices[i].uv = act_submesh.vertices[i].texcoord;
                }

                auto material_id = material_map[act_submesh.material];
                if (!material_id.has_value()) {
                    LogError("model: invalid material for act submesh {}", act_submesh_id);
                    return;
                }

                auto mesh_id = model->addMesh(vertices, act_submesh.indices, material_id.value());
                submesh_map[act_submesh_id] = mesh_id;
            },
                    [&](const act::Model::RiggedSubmesh &act_submesh) {
                std::vector<graphics::Renderer::StaticVertex> vertices{act_submesh.vertices.size()};
                for (size_t i = 0; i < act_submesh.vertices.size(); ++i) {
                    vertices[i].position = act_submesh.vertices[i].position;
                    vertices[i].normal = act_submesh.vertices[i].normal;
                    vertices[i].tangent = act_submesh.vertices[i].tangent;
                    vertices[i].uv = act_submesh.vertices[i].texcoord;
                }

                auto material_id = material_map[act_submesh.material];
                if (!material_id.has_value()) {
                    LogError("model: invalid material for act submesh {}", act_submesh_id);
                    return;
                }

                auto mesh_id = model->addMesh(vertices, act_submesh.indices, material_id.value());
                submesh_map[act_submesh_id] = mesh_id;
            },
                },
                act_any_submesh);
        }
    }

    // node initialization
    std::queue<uint32_t> node_queue;
    for (uint32_t id = 0; id < num_nodes; ++id) {
        node_queue.push(id);
    }

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

        if (act_node.mesh_id.has_value()) {
            const auto &act_mesh = act_model.meshes[act_node.mesh_id.value()];
            for (const auto &act_submesh_id : act_mesh.submesh_ids) {
                auto mesh_id = submesh_map[act_submesh_id];
                if (!mesh_id.has_value()) {
                    continue;
                }

                node->addMesh(mesh_id.value());
            }
        }
    }

    if (!node_queue.empty()) {
        LogError("model: act model has invalid topology");
    }

    return model;
}

} // namespace graphics
