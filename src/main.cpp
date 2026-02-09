#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "logger.hpp"
#include "util.hpp"
#include "vulkan.hpp"
#include "renderer.hpp"
#include "act.hpp"
#include "assets.hpp"

#define GLFW_FATAL_ERROR(glfw_call_name)                                                                               \
    do {                                                                                                               \
        const char *glfw_last_error_msg = nullptr;                                                                     \
        const auto glfw_last_error_code = glfwGetError(&glfw_last_error_msg);                                          \
        if (GLFW_NO_ERROR != glfw_last_error_code) {                                                                   \
            LogError(                                                                                                  \
                "glfw error [{}]: code = {}, message = {}", glfw_call_name, glfw_last_error_code,                      \
                glfw_last_error_msg);                                                                                  \
            util::reportFatalError(                                                                                    \
                fmt::format(                                                                                           \
                    "glfw error [{}]: code = {}, message = {}", glfw_call_name, glfw_last_error_code,                  \
                    glfw_last_error_msg));                                                                             \
            exit(EXIT_FAILURE);                                                                                        \
        }                                                                                                              \
    } while (false);

auto getInstanceExtensionList() -> std::vector<const char *> {
    uint32_t num_instance_extensions = 0;
    const auto instance_extensions = glfwGetRequiredInstanceExtensions(&num_instance_extensions);
    if (!instance_extensions) {
        GLFW_FATAL_ERROR("glfwGetRequiredInstanceExtensions");
    }

    return std::vector<const char *>{instance_extensions, instance_extensions + num_instance_extensions};
}

struct ApplicationState final {
public:
    struct Description {
        std::string_view title;
        uint32_t window_width;
        uint32_t window_height;
    };

    static auto create(const Description &description) -> std::unique_ptr<ApplicationState>;

    ~ApplicationState() noexcept;

    ApplicationState(const ApplicationState &) = delete;
    auto operator=(const ApplicationState &) = delete;

    ApplicationState(ApplicationState &&) noexcept = delete;
    auto operator=(ApplicationState &&) noexcept = delete;

    auto run() -> util::Result;

private:
    class ShaderLoaderImpl : public graphics::Renderer::IShaderLoader {
    public:
        ShaderLoaderImpl(const asset::ArchiveReader *archive);
        ~ShaderLoaderImpl() = default;

        auto loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> override;

    private:
        const asset::ArchiveReader *archive_ = nullptr;
    };

    auto loadMeshes() -> util::Result;

    ApplicationState() = default;

    std::unique_ptr<asset::MainArchive> main_archive_ = {};

    GLFWwindow *window_handle_ = nullptr;
    std::unique_ptr<graphics::Instance> graphics_instance_ = {};
    std::unique_ptr<graphics::Context> graphics_context_ = {};
    std::unique_ptr<graphics::Renderer> renderer_ = {};

    std::optional<graphics::Renderer::MeshId> test_mesh_ = {};
};

auto main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) -> int {
#if !defined(NDEBUG) && defined(_WIN32)
    // report memory leaks on windows msvc debug
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    LogInfo("compiled with {} on {}", COMPILER_STRING, __DATE__);

    ApplicationState::Description app_desc = {
        .title = "vulkan 1.3 dynamic rendering",
        .window_width = 1366,
        .window_height = 768,
    };

    auto app_state = ApplicationState::create(app_desc);
    if (!app_state) {
        return EXIT_FAILURE;
    }

    auto result = app_state->run();
    if (util::Result::eSuccess != result) {
        util::reportFatalError("application returned error code");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

ApplicationState::ShaderLoaderImpl::ShaderLoaderImpl(const asset::ArchiveReader *archive) : archive_{archive} {}

auto ApplicationState::ShaderLoaderImpl::loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> {
    const auto buffer = archive_->getFileContent("shader.spv");
    if (!buffer) {
        LogError("failed to load shader.spv");
        return std::nullopt;
    }

    constexpr auto kWordSize = sizeof(uint32_t);
    if (buffer->size() % kWordSize != 0) {
        LogError("invalid alignment of spir-v bytecode");
        return std::nullopt;
    }

    const auto size_in_words = buffer->size() / kWordSize;
    std::vector<uint32_t> spv_buffer;

    spv_buffer.resize(size_in_words);
    ::memcpy(spv_buffer.data(), buffer->data(), size_in_words * sizeof(uint32_t));

    return spv_buffer;
}

auto ApplicationState::create(const Description &description) -> std::unique_ptr<ApplicationState> {
    std::unique_ptr<ApplicationState> state{new (std::nothrow) ApplicationState()};
    if (!state) {
        LogError("cannot allocate new application state");
        util::reportFatalError("cannot allocate new application state");

        return nullptr;
    }

    state->main_archive_ = asset::MainArchive::create();
    if (!state->main_archive_) {
        LogError("fatal: cannot open main archive");
        util::reportFatalError("cannot open main asset archive");

        return nullptr;
    }

    if (!glfwInit()) {
        GLFW_FATAL_ERROR("glfwInit");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);

    LogInfo("glfw: create window");
    state->window_handle_ = glfwCreateWindow(
        description.window_width, description.window_height, std::string{description.title}.c_str(), nullptr, nullptr);
    if (!state->window_handle_) {
        LogError("glfw: create window");
        GLFW_FATAL_ERROR("glfwCreateWindow");
    }

    glfwSetWindowUserPointer(state->window_handle_, static_cast<void *>(state.get()));

    graphics::Instance::Description graphics_instance_desc = {
        .instance_extensions = getInstanceExtensionList(),
    };

    state->graphics_instance_ = graphics::Instance::create(graphics_instance_desc);
    LogInfo("created graphics instance");

    int32_t window_width = 0, window_height = 0;
    int32_t framebuffer_width = 0, framebuffer_height = 0;

    glfwGetWindowSize(state->window_handle_, &window_width, &window_height);
    glfwGetFramebufferSize(state->window_handle_, &framebuffer_width, &framebuffer_height);

    LogInfo("window extent is {}x{}", window_width, window_height);
    LogInfo("framebuffer extent is {}x{}", framebuffer_width, framebuffer_height);

    graphics::Context::Description context_desc = {
        .surface = VK_NULL_HANDLE,
        .surface_extent = {static_cast<uint32_t>(window_width), static_cast<uint32_t>(window_height)},
        .framebuffer_extent = {static_cast<uint32_t>(framebuffer_width), static_cast<uint32_t>(framebuffer_height)},
    };

    VK_CHECK_ERROR(glfwCreateWindowSurface(
        state->graphics_instance_->instance(), state->window_handle_, nullptr, &context_desc.surface));

    LogInfo("created window vulkan surface");

    state->graphics_context_ = state->graphics_instance_->createContext(context_desc);
    LogInfo("created graphics context");

    graphics::Renderer::Description renderer_desc = {
        .context = state->graphics_context_.get(),
        .shader_loader = std::make_unique<ShaderLoaderImpl>(&state->main_archive_->reader()),
    };

    state->renderer_ = graphics::Renderer::create(renderer_desc);
    LogInfo("created renderer object");

    if (util::Result::eSuccess != state->loadMeshes()) {
        LogError("failed to load resources");
        return nullptr;
    }

    return state;
}

auto ApplicationState::loadMeshes() -> util::Result {
    const auto act_buffer = main_archive_->reader().getFileContent("wakamo.act");
    if (!act_buffer.has_value()) {
        LogError("failed to read wakamo.act");
        util::reportFatalError("missing important resources");

        return util::Result::eFailure;
    }

    const auto model = act::Model::loadFromBinary(act_buffer.value());

    if (!model.has_value()) {
        LogError("failed to load act model");
        return util::Result::eFailure;
    }

    LogInfo("model has {} nodes", model->nodes.size());

    // get the first mesh and translate into a buffer for renderer
    const auto &submesh_id = model->meshes[0].submesh_ids[0];
    const auto &submesh = std::get<act::Model::RiggedSubmesh>(model->submeshes[submesh_id]);

    std::vector<graphics::Renderer::StaticVertex> vertices{submesh.vertices.size()};
    for (size_t i = 0; i < submesh.vertices.size(); ++i) {
        vertices[i].position = submesh.vertices[i].position;
        vertices[i].normal = submesh.vertices[i].normal;
        vertices[i].tangent = submesh.vertices[i].tangent;
        vertices[i].uv = submesh.vertices[i].texcoord;
    }

    test_mesh_ = renderer_->createMesh(vertices, submesh.indices);
    if (!test_mesh_) {
        LogError("failed to upload gpu mesh");
        return util::Result::eFailure;
    }

    return util::Result::eSuccess;
}

auto ApplicationState::run() -> util::Result {
    while (!glfwWindowShouldClose(window_handle_)) {
        graphics::Renderer::OpaqueDrawDescription draw_desc = {
            .mesh = test_mesh_.value(),
            .world_matrix = glm::fmat4x4{1.0f},
        };

        renderer_->drawOpaqueMesh(std::move(draw_desc));

        if (util::Result::eSuccess != renderer_->frame()) {
            LogError("failed to render frame");
            return util::Result::eFailure;
        }

        glfwPollEvents();
    }

    return util::Result::eSuccess;
}

ApplicationState::~ApplicationState() noexcept {
    if (window_handle_) {
        glfwDestroyWindow(window_handle_);
        window_handle_ = nullptr;
    }

    glfwTerminate();
}
