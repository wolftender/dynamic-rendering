#pragma once
#undef near
#undef far

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

namespace graphics {

class Camera final {
public:
    Camera()
        : position_{0.0f, 0.0f, 1.0f}, target_{0.0f, 0.0f, 0.0f}, aspect_{1.0f}, fov_{glm::pi<float>() * 0.5f},
          near_{0.5f}, far_{200.0f}, dirty_bit_proj_{true}, dirty_bit_view_{true}, projection_{1.0f},
          projection_inv_{1.0f}, view_{1.0f}, view_inv_{1.0f} {}

    auto position() const -> const glm::fvec3 & { return position_; }
    auto target() const -> const glm::fvec3 & { return target_; }
    auto aspect() const -> float { return aspect_; }
    auto fov() const -> float { return fov_; }
    auto near() const -> float { return near_; }
    auto far() const -> float { return far_; }

    auto setPosition(const glm::fvec3 &position) -> void;
    auto setTarget(const glm::fvec3 &target) -> void;
    auto setAspect(float aspect) -> void;
    auto setFov(float fov) -> void;
    auto setNear(float near) -> void;
    auto setFar(float far) -> void;
    auto projection() const -> const glm::fmat4x4 &;
    auto view() const -> const glm::fmat4x4 &;
    auto projectionInv() const -> const glm::fmat4x4 &;
    auto viewInv() const -> const glm::fmat4x4 &;

private:
    inline auto calculateProjection() const -> void;
    inline auto calculateView() const -> void;

    glm::fvec3 position_;
    glm::fvec3 target_;

    float aspect_, fov_, near_, far_;

    // cache
    mutable bool dirty_bit_proj_;
    mutable bool dirty_bit_view_;
    mutable glm::fmat4x4 projection_, projection_inv_;
    mutable glm::fmat4x4 view_, view_inv_;
};

} // namespace graphics