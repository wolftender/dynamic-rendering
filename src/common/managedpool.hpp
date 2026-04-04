#pragma once
#include <array>
#include <cstdint>
#include <optional>

#include "common/circularqueue.hpp"

namespace util {

template <typename T, uint32_t kPoolSize, typename Tag = T> class ManagedPool {
public:
    class Id final {
    public:
        using Resource = T;

        Id(uint32_t index, uint32_t generation) : index_{index}, generation_{generation} {}

        auto index() const -> uint32_t { return index_; }
        auto generation() const -> uint32_t { return generation_; }

    private:
        uint32_t index_;
        uint32_t generation_;
    };

    ManagedPool() {
        for (uint32_t i = 0; i < kPoolSize; ++i) {
            storage_[i].resource = std::nullopt;
            storage_[i].generation = 0;
            storage_[i].identifier = i;
        }
    }

    auto store(T resource) -> std::optional<Id> {
        auto index = free_ids_.pop();
        if (!index.has_value()) {
            return std::nullopt;
        }

        auto &slot = storage_[index.value()];
        auto id = Id{index.value(), slot.generation};

        slot.resource = std::move(resource);
        return id;
    }

    auto get(const Id &id) -> T * {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return nullptr;
        }

        if (!slot.resource.has_value()) {
            return nullptr;
        }

        return &slot.resource.value();
    }

    auto get(const Id &id) const -> const T * {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return nullptr;
        }

        if (!slot.resource.has_value()) {
            return nullptr;
        }

        return &slot.resource.value();
    }

    auto destroy(const Id &id) -> void {
        auto &slot = storage_[id.index()];
        if (id.generation() != slot.generation) {
            return;
        }

        if (!slot.resource.has_value()) {
            return;
        }

        slot.generation++;
    }

private:
    struct Slot {
        std::optional<T> resource;
        uint32_t identifier;
        uint32_t generation;
    };

    std::array<Slot, kPoolSize> storage_;
    util::CircularBufferQueue<uint32_t, kPoolSize> free_ids_;
};

} // namespace util
