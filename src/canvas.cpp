#include "canvas.hpp"
#include "logger.hpp"

namespace graphics {

[[maybe_unused]] const float PI = 3.14159265359f;
[[maybe_unused]] const float PI2 = 6.28318530718f;
[[maybe_unused]] const float HPI = 1.57079632679f;

namespace geometry {

inline auto rectContains(const glm::fvec2 &min, const glm::fvec2 &max, const glm::fvec2 &point) -> bool {
    return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
}

inline auto orthogonal(const glm::fvec2 &vec) -> glm::fvec2 { return {-vec.y, vec.x}; }

inline auto
decasteljeu(const glm::fvec2 &p00, const glm::fvec2 &p01, const glm::fvec2 &p02, const glm::fvec2 &p03, float t)
    -> glm::fvec2 {
    const auto t0 = t;
    const auto t1 = 1.0f - t;

    const auto p10 = t0 * p00 + t1 * p01;
    const auto p11 = t0 * p01 + t1 * p02;
    const auto p12 = t0 * p02 + t1 * p03;

    const auto p20 = t0 * p10 + t1 * p11;
    const auto p21 = t0 * p11 + t1 * p12;

    return t0 * p20 + t1 * p21;
}

inline auto intersect(const glm::fvec2 &p1, const glm::fvec2 &p2, const glm::fvec2 &p3, const glm::fvec2 &p4)
    -> std::optional<float> {
    const auto num = (p1.x - p3.x) * (p3.y - p4.y) - (p1.y - p3.y) * (p3.x - p4.x);
    const auto den = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);

    if (fabsf(den) < Canvas::kCompareTolerance) {
        return {};
    }

    return num / den;
}

inline auto angleCCW(const glm::fvec2 &a, const glm::fvec2 &b) -> float {
    const auto dot = glm::dot(a, b);
    const auto det = a.x * b.y - a.y * b.x;
    const auto angle = atan2f(det, dot);

    if (angle < 0.0f) {
        return (glm::pi<float>() * 2.0f) + angle;
    }

    return angle;
}

inline auto isConvex(const glm::fvec2 &v0, const glm::fvec2 &v1, const glm::fvec2 &v2) -> bool {
    const auto a = v0 - v1;
    const auto b = v2 - v1;

    return angleCCW(b, a) <= glm::pi<float>();
}

inline auto isInTriangle(const glm::fvec2 &a, const glm::fvec2 &b, const glm::fvec2 &c, const glm::fvec2 &p) -> bool {
    const auto v0 = c - a;
    const auto v1 = b - a;
    const auto v2 = p - a;

    const auto dot00 = glm::dot(v0, v0);
    const auto dot01 = glm::dot(v0, v1);
    const auto dot02 = glm::dot(v0, v2);
    const auto dot11 = glm::dot(v1, v1);
    const auto dot12 = glm::dot(v1, v2);

    const auto den = dot00 * dot11 - dot01 * dot01;
    if (fabsf(den) < Canvas::kCompareTolerance) {
        return true;
    }

    const auto inv_den = 1.0f / den;
    const auto u = (dot11 * dot02 - dot01 * dot12) * inv_den;
    const auto v = (dot00 * dot12 - dot01 * dot02) * inv_den;

    return (u >= 0) && (v >= 0) && (u + v < 1.0);
}

inline auto cross(const glm::fvec2 &a, const glm::fvec2 &b) -> float { return a.x * b.y - a.y * b.x; }

}; // namespace geometry

auto Canvas::Path::appendVertex(const glm::vec2 &position) -> void {
    vertices_.emplace_back(Vertex{position, color_, width_, line_cap_, line_joint_});
}

auto Canvas::Path::appendBezier(const glm::fvec2 &c1, const glm::fvec2 &c2, const glm::fvec2 &p) -> void {
    if (vertices_.empty()) {
        return;
    }

    const auto &vs = vertices_.back();
    auto ve = Vertex{p, color_, width_, line_cap_, line_joint_};

    tesselateBezier(vs, c1, c2, ve, 0, kTesselationTolerance, kCompareTolerance);
}

auto Canvas::Path::appendArc(const glm::fvec2 &center, float theta) -> void {
    const auto &vs = vertices_.back();
    const auto sp = vs.position;
    const auto v1 = sp - center;
    const auto R = glm::length(v1);
    const auto len = R * theta;
    const auto segments = static_cast<int>(floor(len / kTesselationTolerance));
    const auto dtheta = theta / segments;

    for (int i = 1; i <= segments; ++i) {
        const auto ang = dtheta * i;
        const auto c = cosf(ang);
        const auto s = sinf(ang);

        const auto v = glm::fvec2{
            v1.x * c - v1.y * s,
            v1.x * s + v1.y * c,
        };

        float t = static_cast<float>(segments) / static_cast<float>(i);
        const auto int_color = glm::mix(vs.color, color_, t);
        const auto int_width = glm::mix(vs.width, width_, t);

        vertices_.emplace_back(
            Vertex{v, int_color, int_width, line_cap_, i == segments ? line_joint_ : LineJoint::eBevel});
    }
}

auto Canvas::Path::closeContour() -> void {
    if (vertices_.size() < 2) {
        return;
    }

    if (closed()) {
        return;
    }

    appendVertex(vertices_.front().position);
}

auto Canvas::Path::clearContour() -> void { vertices_.clear(); }

auto Canvas::Path::closed() const -> bool {
    if (vertices_.size() < 3) {
        return false;
    }

    return (glm::distance(vertices_.front().position, vertices_.back().position) <= kCompareTolerance);
}

auto Canvas::Path::signedArea() const -> std::optional<float> {
    if (!closed()) {
        return std::nullopt;
    }

    float signed_area = 0.0f;
    for (size_t i = 0; i < vertices_.size(); ++i) {
        const auto &v0 = vertices_[i].position;
        const auto &v1 = vertices_[(i + 1) % vertices_.size()].position;

        signed_area += (v0.x * v1.y - v1.x * v0.y);
    }

    return {signed_area * 0.5f};
}

auto Canvas::Path::appendVerticesRange(std::span<const glm::vec2> vertices) -> void {
    for (const auto &position : vertices) {
        vertices_.emplace_back(Vertex{position, color_, width_, line_cap_, line_joint_});
    }
}

auto Canvas::Path::createFill(Canvas &canvas) const -> std::optional<ObjectId> {
    const auto mesh = createFillMesh();
    return canvas.createPolygon(mesh.vertices, mesh.indices);
}

auto Canvas::Path::createStroke(Canvas &canvas) const -> std::optional<ObjectId> {
    const auto mesh = createStrokeMesh();
    return canvas.createPolygon(mesh.vertices, mesh.indices);
}

auto Canvas::Path::tesselateBezier(
    const Vertex &vs, const glm::fvec2 &p1, const glm::fvec2 &p2, const Vertex &ve, uint32_t level, float tess_tol,
    float dist_tol) -> void {
    if (level > 10) {
        return;
    }

    const auto p0 = vs.position;
    const auto p3 = ve.position;

    const auto p01 = (p0 + p1) * 0.5f;
    const auto p12 = (p1 + p2) * 0.5f;
    const auto p23 = (p2 + p3) * 0.5f;
    const auto p012 = (p01 + p12) * 0.5f;
    const auto d = p3 - p0;

    float d2 = std::fabsf((p1.x - p3.x) * d.y - (p1.y - p3.y) * d.x);
    float d3 = std::fabsf((p2.x - p3.x) * d.y - (p2.y - p3.y) * d.y);

    if ((d2 + d3) * (d2 + d3) < tess_tol * glm::dot(d, d)) {
        vertices_.emplace_back(ve);
        return;
    }

    const auto p123 = (p12 + p23) * 0.5f;
    const auto p0123 = (p012 + p123) * 0.5f;

    const auto int_color = 0.5f * (vs.color + ve.color);
    const auto int_width = 0.5f * (vs.width + ve.width);

    const auto vm = Vertex{p0123, int_color, int_width, line_cap_, level > 0 ? LineJoint::eBevel : line_joint_};

    tesselateBezier(vs, p01, p012, vm, level + 1, tess_tol, dist_tol);
    tesselateBezier(vm, p123, p23, ve, level + 1, tess_tol, dist_tol);
}

auto Canvas::Path::createStrokeMesh() const -> Mesh {
    const auto num_vertices = static_cast<uint32_t>(vertices_.size());
    Mesh mesh;

    const auto tesselate_arc = [&](uint32_t center_idx, uint32_t start_idx, uint32_t end_idx, uint32_t origin_idx) {
        // https://hansmuller-flex.blogspot.com/2011/10/more-about-approximating-circular-arcs.html
        const auto vtx_start = mesh.vertices[start_idx];
        const auto vtx_end = mesh.vertices[end_idx];
        const auto vtx_center = mesh.vertices[center_idx];

        const auto &pos_start = vtx_start.position;
        const auto &pos_end = vtx_end.position;
        const auto &pos_center = vtx_center.position;

        const auto kDiv = 10;
        const auto a = pos_start - pos_center;
        const auto b = pos_end - pos_center;
        const auto q1 = glm::dot(a, a);
        const auto q2 = q1 + glm::dot(a, b);
        const auto k2 = (4.0f / 3.0f) * (sqrtf(2.0f * q1 * q2) - q2) / geometry::cross(a, b);

        const auto x2 = pos_center.x + a.x - k2 * a.y;
        const auto y2 = pos_center.y + a.y + k2 * a.x;
        const auto x3 = pos_center.x + b.x + k2 * b.y;
        const auto y3 = pos_center.y + b.y - k2 * b.x;

        const auto p00 = pos_start;
        const auto p01 = glm::fvec2{x2, y2};
        const auto p02 = glm::fvec2{x3, y3};
        const auto p03 = pos_end;
        const auto dt = 1.0f / static_cast<float>(kDiv);

        auto prev_idx = start_idx;

        for (auto i = 1; i < kDiv; ++i) {
            const auto ti = dt * static_cast<float>(i);
            const auto posi = geometry::decasteljeu(p00, p01, p02, p03, 1.0f - ti);
            const auto colori = glm::mix(vtx_start.color, vtx_end.color, ti);

            const auto iidx = mesh.appendVertex(Renderer::VectorVertex{glm::fvec3{posi, 0.0f}, colori});
            mesh.appendTriangle(prev_idx, iidx, origin_idx);

            prev_idx = iidx;
        }

        mesh.appendTriangle(prev_idx, end_idx, origin_idx);
    };

    const auto tesselate_stroke_fragment = [&](const Vertex &v0, const Vertex &v1, const Vertex &v2) {
        const auto &p0 = v0.position;
        const auto &p1 = v1.position;
        const auto &p2 = v2.position;

        const auto w0 = v0.width * 0.5f;
        const auto w1 = v1.width * 0.5f;
        const auto w2 = v2.width * 0.5f;

        const auto v01 = p1 - p0;
        const auto v12 = p2 - p1;
        const auto tan0 = glm::normalize(geometry::orthogonal(v01));
        const auto tan2 = glm::normalize(geometry::orthogonal(v12));

        glm::fvec2 p0i, p1i, p2i; // arc interior point
        glm::fvec2 p0e, p1e, p2e; // arc exterior point
        glm::fvec2 p1t0, p1t2;    // outer tangents for p1

        const auto c = geometry::cross(v01, v12);
        if (c < 0.0f) {
            p0i = p0 - (w0 * tan0);
            p2i = p2 - (w2 * tan2);
            p0e = p0 + (w0 * tan0);
            p2e = p2 + (w2 * tan2);
            p1t0 = p1 + (w1 * tan0);
            p1t2 = p1 + (w1 * tan2);
        } else {
            p0i = p0 + (w0 * tan0);
            p2i = p2 + (w2 * tan2);
            p0e = p0 - (w0 * tan0);
            p2e = p2 - (w2 * tan2);
            p1t0 = p1 - (w1 * tan0);
            p1t2 = p1 - (w1 * tan2);
        }

        const auto pp0 = p0i + v01;
        const auto pp2 = p2i - v12;
        const auto col = geometry::intersect(p0i, pp0, p2i, pp2).value_or(-1000.0f); // some bogus value for parallel

        p1i = p0i + col * v01;
        p1e = p1 - (p1i - p1);

        uint32_t idx_p0i = mesh.appendVertex(Renderer::VectorVertex{p0i, v0.color});
        uint32_t idx_p0e = mesh.appendVertex(Renderer::VectorVertex{p0e, v0.color});
        uint32_t idx_p2i = mesh.appendVertex(Renderer::VectorVertex{p2i, v2.color});
        uint32_t idx_p2e = mesh.appendVertex(Renderer::VectorVertex{p2e, v2.color});

        if (v0.line_cap == LineCap::eRound) {
            const auto p0d = p0 + (w0 * glm::normalize(p0 - p1));
            uint32_t idx_p0d = mesh.appendVertex(Renderer::VectorVertex{p0d, v0.color});
            uint32_t idx_p0 = mesh.appendVertex(Renderer::VectorVertex{p0, v0.color});
            tesselate_arc(idx_p0, idx_p0i, idx_p0d, idx_p0);
            tesselate_arc(idx_p0, idx_p0d, idx_p0e, idx_p0);
        }

        if (v2.line_cap == LineCap::eRound) {
            const auto p2d = p2 + (w2 * glm::normalize(p2 - p1));
            uint32_t idx_p2d = mesh.appendVertex(Renderer::VectorVertex{p2d, v2.color});
            uint32_t idx_p2 = mesh.appendVertex(Renderer::VectorVertex{p2, v2.color});
            tesselate_arc(idx_p2, idx_p2i, idx_p2d, idx_p2);
            tesselate_arc(idx_p2, idx_p2d, idx_p2e, idx_p2);
        }

        if (col < 0.0f) { // degenerated case with no p1i point
            uint32_t idx_p1t0 = mesh.appendVertex(Renderer::VectorVertex{p1t0, v1.color});
            uint32_t idx_p1t2 = mesh.appendVertex(Renderer::VectorVertex{p1t2, v1.color});

            mesh.appendTriangle(idx_p0i, idx_p0e, idx_p1t0);
            mesh.appendTriangle(idx_p0i, idx_p1t0, idx_p1t2);
            mesh.appendTriangle(idx_p2e, idx_p1t0, idx_p2i);
            mesh.appendTriangle(idx_p2e, idx_p1t2, idx_p1t0);

            if (v1.line_joint == LineJoint::eRound) {
                const auto p1n = p1 + (w1 * glm::normalize(v01 - v12));
                const auto p1c = 0.5f * (p1t0 + p1t2);

                uint32_t idx_p1 = mesh.appendVertex(Renderer::VectorVertex{p1, v1.color});
                uint32_t idx_p1n = mesh.appendVertex(Renderer::VectorVertex{p1n, v1.color});
                uint32_t idx_p1c = mesh.appendVertex(Renderer::VectorVertex{p1c, v1.color});

                tesselate_arc(idx_p1, idx_p1t0, idx_p0e, idx_p1c);
                tesselate_arc(idx_p1, idx_p1n, idx_p1t2, idx_p1c);
            }
        } else {
            uint32_t idx_p1i = mesh.appendVertex(Renderer::VectorVertex{p1i, v1.color});
            uint32_t idx_p1e = mesh.appendVertex(Renderer::VectorVertex{p1e, v1.color});
            uint32_t idx_p1t0 = mesh.appendVertex(Renderer::VectorVertex{p1t0, v1.color});
            uint32_t idx_p1t2 = mesh.appendVertex(Renderer::VectorVertex{p1t2, v1.color});

            mesh.appendTriangle(idx_p0i, idx_p1t0, idx_p0e);
            mesh.appendTriangle(idx_p0i, idx_p1i, idx_p1t0);
            mesh.appendTriangle(idx_p1i, idx_p2i, idx_p1t2);
            mesh.appendTriangle(idx_p2e, idx_p2i, idx_p1t2);

            if (v1.line_joint == LineJoint::eBevel || v1.line_joint == LineJoint::eMiter) {
                mesh.appendTriangle(idx_p1t0, idx_p1i, idx_p1t2);
            }

            if (v1.line_joint == LineJoint::eMiter) {
                mesh.appendTriangle(idx_p1t2, idx_p1t0, idx_p1e);
            } else if (v1.line_joint == LineJoint::eRound) {
                uint32_t idx_p1 = mesh.appendVertex(Renderer::VectorVertex{p1, v1.color});
                tesselate_arc(idx_p1, idx_p1t0, idx_p1t2, idx_p1i);
            }
        }
    };

    if (vertices_.size() > 2) {
        auto vm1 = Vertex{vertices_[1], vertices_[2], 0.5f};

        tesselate_stroke_fragment(vertices_[0], vertices_[1], vm1);

        for (uint32_t idx = 2; idx < num_vertices - 2; ++idx) {
            const auto &v = vertices_[idx];
            auto vm2 = Vertex{v, vertices_[idx + 1], 0.5f};

            vm2.line_cap = LineCap::eButt;

            tesselate_stroke_fragment(vm1, v, vm2);
            vm1 = vm2;
        }

        vm1 = Vertex{vertices_[num_vertices - 3], vertices_[num_vertices - 2], 0.5f};
        tesselate_stroke_fragment(vm1, vertices_[num_vertices - 2], vertices_[num_vertices - 1]);
    } else {
        // TODO
    }

    return mesh;
}

auto Canvas::Path::createFillMesh() const -> Mesh {
    Mesh mesh;
    if (vertices_.size() < 3) {
        return mesh;
    }

    if (!closed()) {
        return mesh;
    }

    struct VertexListEntry {
        uint32_t index;
        VertexListEntry *prev;
        VertexListEntry *next;

        auto erase() -> VertexListEntry * {
            prev->next = next;
            next->prev = prev;

            return next;
        }
    };

    std::vector<VertexListEntry> vertex_list{vertices_.size()};
    const uint32_t num_vertices = static_cast<uint32_t>(vertices_.size());
    const uint32_t num_vertices_real = num_vertices - 1;

    mesh.vertices.reserve(vertices_.size());
    mesh.indices.reserve(vertices_.size() * 3);

    if (signedArea().value() >= 0.0f) {
        for (uint32_t i = 0; i < vertices_.size() - 1; ++i) {
            mesh.appendVertex(Renderer::VectorVertex{vertices_[i].position, vertices_[i].color});

            vertex_list[i].prev = &vertex_list[(num_vertices_real + i - 1) % num_vertices_real];
            vertex_list[i].next = &vertex_list[(i + 1) % num_vertices_real];
            vertex_list[i].index = i;
        }
    } else {
        for (uint32_t i = 0; i < vertices_.size() - 1; ++i) {
            mesh.appendVertex(Renderer::VectorVertex{vertices_[i].position, vertices_[i].color});

            vertex_list[i].prev = &vertex_list[(num_vertices_real + i - 1) % num_vertices_real];
            vertex_list[i].next = &vertex_list[(i + 1) % num_vertices_real];
            vertex_list[i].index = num_vertices_real - i - 1;
        }
    }

    VertexListEntry *iter = &vertex_list[0];

    uint32_t remaining_vertices = num_vertices_real;
    for (uint32_t ear_tries = 0; ear_tries < remaining_vertices + 2 && remaining_vertices > 2; ++ear_tries) {
        uint32_t i0, i1, i2;
        i1 = iter->index;
        i0 = iter->prev->index;
        i2 = iter->next->index;

        const auto &v0 = vertices_[i0];
        const auto &v1 = vertices_[i1];
        const auto &v2 = vertices_[i2];

        const auto &p0 = v0.position;
        const auto &p1 = v1.position;
        const auto &p2 = v2.position;

        LogInfo("checking [{}, {}] [{}, {}] [{}, {}]", p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);

        if (!geometry::isConvex(p0, p1, p2)) {
            LogInfo("triangle {} {} {} is not convex", i0, i1, i2);
            iter = iter->next;
            continue;
        }

        const auto *other = iter->next->next;
        auto is_ear = true;

        for (uint32_t i = 0; i < remaining_vertices - 3; ++i) {
            const auto &p = vertices_[other->index].position;
            if (geometry::isInTriangle(p0, p1, p2, p)) {
                is_ear = false;
                break;
            }

            other = other->next;
        }

        if (!is_ear) {
            LogInfo("triangle {} {} {} is not an ear", i0, i1, i2);
            iter = iter->next;
            continue;
        }

        ear_tries = 0;

        LogInfo("add triangle {} {} {}", i0, i1, i2);
        mesh.appendTriangle(i0, i1, i2);
        iter = iter->erase();
        remaining_vertices--;
    }

    return mesh;
}

auto Canvas::create(const Description &desc) -> std::unique_ptr<Canvas> {
    std::unique_ptr<Canvas> canvas{new Canvas(desc.renderer)};
    if (!canvas) {
        LogError("canvas: failed to allocate canvas");
        return nullptr;
    }

    return canvas;
}

auto Canvas::getObject(ObjectId id) const -> const Object * {
    const Object *res = nullptr;
    objects_.with(id, [&](const auto &object) { res = &object; });

    return res;
}

auto Canvas::getObject(ObjectId id) -> Object * {
    Object *res = nullptr;
    objects_.withMut(id, [&](auto &object) { res = &object; });

    return res;
}

auto Canvas::destroyObject(ObjectId id) -> void { objects_.destroy(id); }

auto Canvas::draw() const -> void {
    objects_.iterate([&](const auto &object) {
        Renderer::VectorDrawDescription desc = {
            .vector_mesh = object.mesh(),
            .world_matrix = object.transform(),
        };

        renderer_->drawVectorMesh(std::move(desc));
    });
}

auto Canvas::createPolygon(std::span<const Renderer::VectorVertex> vertices, std::span<const uint32_t> indices)
    -> std::optional<ObjectId> {
    if (vertices.size() == 0 || indices.size() == 0) {
        return std::nullopt;
    }

    auto mesh_id = renderer_->createVectorMesh(vertices, indices);
    if (!mesh_id.has_value()) {
        return std::nullopt;
    }

    return objects_.insert(Object{mesh_id.value()});
}

} // namespace graphics
