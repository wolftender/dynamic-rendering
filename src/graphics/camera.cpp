#include "camera.hpp"

namespace graphics {

auto Camera::setPosition(const glm::fvec3 &position) -> void {
    position_ = position;
    dirty_bit_view_ = true;
}

auto Camera::setTarget(const glm::fvec3 &target) -> void {
    target_ = target;
    dirty_bit_view_ = true;
}

auto Camera::setAspect(float aspect) -> void {
    aspect_ = aspect;
    dirty_bit_proj_ = true;
}

auto Camera::setFov(float fov) -> void {
    fov_ = fov;
    dirty_bit_proj_ = true;
}

auto Camera::setNear(float near) -> void {
    near_ = near;
    dirty_bit_proj_ = true;
}

auto Camera::setFar(float far) -> void {
    far_ = far;
    dirty_bit_proj_ = true;
}

auto Camera::projection() const -> const glm::fmat4x4 & {
    calculateProjection();
    return projection_;
}

auto Camera::view() const -> const glm::fmat4x4 & {
    calculateView();
    return view_;
}

auto Camera::projectionInv() const -> const glm::fmat4x4 & {
    calculateProjection();
    return projection_inv_;
}

auto Camera::viewInv() const -> const glm::fmat4x4 & {
    calculateView();
    return view_inv_;
}

inline auto Camera::calculateProjection() const -> void {
    if (dirty_bit_proj_) {
        projection_ = glm::perspective(fov_, aspect_, near_, far_);
        projection_inv_ = glm::inverse(projection_);
        dirty_bit_proj_ = false;
    }
}

inline auto Camera::calculateView() const -> void {
    if (dirty_bit_view_) {
        view_ = glm::lookAt(position_, target_, glm::fvec3{0.0f, 1.0f, 0.0f});
        view_inv_ = glm::inverse(view_);
        dirty_bit_view_ = false;
    }
}

} // namespace graphics
