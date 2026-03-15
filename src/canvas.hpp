#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "renderer.hpp"

namespace graphics {

class Canvas final {
public:
    enum class LineCap {
        eButt,
        eRound,
    };

    enum class LineJoint {
        eMiter,
        eBevel,
        eRound,
    };

    static constexpr float kCompareTolerance = 1e-7f;
    static constexpr float kTesselationTolerance = 5.0f;

    template <typename T> struct ObjectContainer final {
    private:
        struct Slot {
            uint32_t self, generation;

            std::optional<T> object;
            Slot *next, *prev;
        };

    public:
        struct Id final {
        public:
            auto index() const -> size_t { return index_; }
            auto generation() const -> size_t { return generation_; }

        private:
            Id(size_t index, size_t generation) : index_{index}, generation_{generation} {}
            size_t index_, generation_;

            friend ObjectContainer;
        };

        ObjectContainer(size_t capacity) {
            pool_.resize(capacity);
            reset();
        }

        auto insert(T &&object) -> Id {
            auto &slot = allocSlot();
            slot.object = std::move(object);

            return Id{slot.self, slot.generation};
        }

        auto destroy(Id id) -> void {
            auto &slot = pool_[id.index()];
            if (slot.generation == id.generation()) {
                freeSlot(id.index());
            }
        }

        template <std::invocable<const T &> F> auto with(Id id, F consumer) const -> void {
            auto &slot = pool_[id.index()];
            if (slot.generation == id.generation() && slot.object) {
                consumer(*slot.object);
            }
        }

        template <std::invocable<T &> F> auto withMut(Id id, F consumer) -> void {
            auto &slot = pool_[id.index()];
            if (slot.generation == id.generation() && slot.object) {
                consumer(*slot.object);
            }
        }

        auto clear() -> void {
            for (size_t i = 0; i < pool_.size(); ++i) {
                freeSlot(i);
            }
        }

        template <std::invocable<const T &> F> auto iterate(F consumer) const -> void {
            for (const Slot *iter = iter_list_; iter; iter = iter->next) {
                if (iter->object) {
                    consumer(*iter->object);
                }
            }
        }

        template <std::invocable<T &> F> auto iterateMut(F consumer) -> void {
            for (Slot *iter = iter_list_; iter; iter = iter->next) {
                if (iter->object) {
                    consumer(*iter->object);
                }
            }
        }

    private:
        auto freeSlot(size_t slot_id) -> void {
            Slot &slot = pool_[slot_id];
            slot.object = {};
            slot.generation++;
            slot.next = free_list_;
            free_list_ = &slot;

            if (slot.next) {
                slot.next->prev = slot.prev;
            }

            if (slot.prev) {
                slot.prev->next = slot.next;
            }

            if (iter_list_ == &slot) {
                iter_list_ = slot.next;
            }
        }

        auto allocSlot() -> Slot & {
            if (!free_list_) {
                throw std::bad_alloc{};
            }

            Slot *slot = free_list_;
            free_list_ = free_list_->next;

            slot->next = nullptr;
            slot->object = {};

            slot->prev = iter_list_;
            if (iter_list_) {
                iter_list_->prev = slot;
            }

            slot->next = iter_list_;
            iter_list_ = slot;

            return *slot;
        }

        auto reset() -> void {
            iter_list_ = nullptr;
            free_list_ = &pool_.front();
            const auto capacity = pool_.size();

            for (size_t i = 0; i < capacity - 1; ++i) {
                pool_[i].self = i;
                pool_[i].next = &pool_[i + 1];
                pool_[i].generation = 0;
                pool_[i].object = {};
            }

            pool_[capacity - 1].self = capacity - 1;
            pool_[capacity - 1].next = nullptr;
            pool_[capacity - 1].generation = 0;
            pool_[capacity - 1].object = {};
        }

        std::vector<Slot> pool_;
        Slot *free_list_;
        Slot *iter_list_;
    };

    class Object final {
    public:
        auto transform() const -> const glm::fmat4x4 & { return transform_; }
        auto mesh() const -> Renderer::VectorMeshId { return mesh_; }

        auto setTransform(const glm::fmat4x4 &matrix) { transform_ = matrix; }

    private:
        Object(Renderer::VectorMeshId mesh) : transform_{1.0f}, mesh_{mesh} {}

        glm::fmat4x4 transform_;
        Renderer::VectorMeshId mesh_;

        friend class Canvas;
    };

    // ids
    using ObjectId = ObjectContainer<Object>::Id;

    class Path final {
    private:
        struct Vertex final {
            glm::fvec2 position;
            glm::fvec4 color;

            float width;

            LineCap line_cap;
            LineJoint line_joint;

            Vertex(
                const glm::fvec2 &position, const glm::fvec4 &color, float width, LineCap line_cap,
                LineJoint line_joint)
                : position{position}, color{color}, width{width}, line_cap{line_cap}, line_joint{line_joint} {}

            Vertex(const Vertex &a, const Vertex &b, float t)
                : position{glm::mix(a.position, b.position, t)}, color{glm::mix(a.color, b.color, t)},
                  width{glm::mix(a.width, b.width, t)}, line_cap{b.line_cap}, line_joint{b.line_joint} {}

            Vertex(const glm::fvec2 &position, const Vertex &a, const Vertex &b, float t)
                : position{position}, color{glm::mix(a.color, b.color, t)}, width{glm::mix(a.width, b.width, t)},
                  line_cap{b.line_cap}, line_joint{b.line_joint} {}
        };

        struct Mesh final {
            std::vector<Renderer::VectorVertex> vertices;
            std::vector<uint32_t> indices;

            auto appendVertex(Renderer::VectorVertex &&v) -> uint32_t {
                vertices.emplace_back(std::move(v));
                return static_cast<uint32_t>(vertices.size() - 1);
            }

            auto appendTriangle(uint32_t i0, uint32_t i1, uint32_t i2) -> void {
                indices.emplace_back(i0);
                indices.emplace_back(i1);
                indices.emplace_back(i2);
            }
        };

    public:
        Path(const glm::fvec4 &color, float width, LineCap line_cap, LineJoint line_joint)
            : color_{color}, width_{width}, line_cap_{line_cap}, line_joint_{line_joint} {}
        ~Path() = default;

        Path(const Path &) = delete;
        auto operator=(const Path &) = delete;

        Path(Path &&) = delete;
        auto operator=(Path &&) = delete;

        auto color() const -> const glm::fvec4 & { return color_; }
        auto width() const -> float { return width_; }
        auto lineCap() const -> LineCap { return line_cap_; }
        auto lineJoint() const -> LineJoint { return line_joint_; }

        auto setColor(const glm::fvec4 &color) -> void { color_ = color; }
        auto setWidth(float width) -> void { width_ = width; }
        auto setLineCap(LineCap line_cap) -> void { line_cap_ = line_cap; }
        auto setLineJoint(LineJoint line_joint) -> void { line_joint_ = line_joint; }

        auto appendVertex(const glm::vec2 &position) -> void;
        auto appendBezier(const glm::fvec2 &c1, const glm::fvec2 &c2, const glm::fvec2 &p) -> void;
        auto appendArc(const glm::fvec2 &center, float theta) -> void;
        auto closeContour() -> void;
        auto clearContour() -> void;
        auto closed() const -> bool;

        auto signedArea() const -> std::optional<float>;

        template <util::TypedForwardRange<glm::vec2> R> auto appendVertices(const R &vertices) -> void {
            const auto num_verts = std::ranges::size(vertices);
            const auto verts_data = std::ranges::data(vertices);

            appendVerticesRange(std::span<const glm::fvec2>{num_verts, verts_data});
        }

        auto appendVerticesRange(std::span<const glm::vec2> vertices) -> void;

        auto createFill(Canvas &canvas) const -> std::optional<ObjectId>;
        auto createStroke(Canvas &canvas) const -> std::optional<ObjectId>;

    private:
        auto tesselateBezier(
            const Vertex &vs, const glm::fvec2 &p1, const glm::fvec2 &p2, const Vertex &ve, uint32_t level,
            float tess_tol, float dist_tol) -> void;

        auto createStrokeMesh() const -> Mesh;
        auto createFillMesh() const -> Mesh;

        glm::fvec4 color_;
        float width_;

        LineCap line_cap_;
        LineJoint line_joint_;

        std::vector<Vertex> vertices_;

        friend class Canvas;
    };

    struct Description {
        Renderer *renderer;
    };

    static auto create(const Description &desc) -> std::unique_ptr<Canvas>;

    ~Canvas() = default;

    Canvas(const Canvas &) = delete;
    auto operator=(const Canvas &) = delete;

    Canvas(Canvas &&) = delete;
    auto operator=(Canvas &&) = delete;

    auto getObject(ObjectId id) const -> const Object *;
    auto getObject(ObjectId id) -> Object *;

    auto destroyObject(ObjectId id) -> void;

    template <std::invocable<const Object &> F> auto withObject(ObjectId id, F consumer) const -> void {
        const auto object = getObject(id);
        if (object) {
            consumer(*object);
        }
    }

    template <std::invocable<Object &> F> auto withObjectMut(ObjectId id, F consumer) -> void {
        const auto object = getObject(id);
        if (object) {
            consumer(*object);
        }
    }

    auto draw() const -> void;

    template <util::TypedForwardRange<Renderer::VectorVertex> VR, util::TypedForwardRange<uint32_t> IR>
    auto createPolygon(const VR &vertices, const IR &indices) -> std::optional<ObjectId> {
        const auto vert_data_size = std::ranges::size(vertices);
        const auto vert_data_ptr = std::ranges::data(vertices);

        const auto idx_data_size = std::ranges::size(indices);
        const auto idx_data_ptr = std::ranges::data(indices);

        return createPolygon(
            std::span<const Renderer::VectorVertex>{vert_data_ptr, vert_data_size},
            std::span<const uint32_t>{idx_data_ptr, idx_data_size});
    }

    auto createPolygon(std::span<const Renderer::VectorVertex> vertices, std::span<const uint32_t> indices)
        -> std::optional<ObjectId>;

private:
    Canvas(Renderer *renderer) : renderer_{renderer}, objects_{4096} {}

    Renderer *renderer_ = nullptr;
    ObjectContainer<Object> objects_;
};

} // namespace graphics
