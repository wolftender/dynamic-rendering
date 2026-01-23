#include "util.hpp"

#include <ios>
#include <fstream>

auto BinaryReader::remaining() const -> u64 { return data_span_.size() - ptr_; }

auto BinaryReader::seek(u64 location) -> Result {
    if (location > data_span_.size()) {
        return Result::eFailure;
    }

    ptr_ = location;
    return Result::eSuccess;
}

auto BinaryReader::readBuffer(u64 num_bytes) -> std::optional<std::span<const u8>> {
    const auto end_ptr = data_span_.size();

    if (ptr_ + num_bytes > end_ptr) {
        return std::nullopt;
    }

    const auto data_ptr = data_span_.data() + ptr_;
    ptr_ += num_bytes;

    return std::span<const u8>{data_ptr, num_bytes};
}

auto readFileToBuffer(const std::string &file_name, std::vector<uint8_t> &buffer) -> Result {
    std::ifstream fs{file_name, std::ios::binary};
    if (!fs.good()) {
        return Result::Fail;
    }

    fs.seekg(0, std::ios::end);
    const auto file_size = static_cast<size_t>(fs.tellg());
    fs.seekg(0, std::ios::beg);

    buffer.resize(file_size);
    fs.read(vecCharPtrMut(buffer), file_size);

    return Result::Success;
}
