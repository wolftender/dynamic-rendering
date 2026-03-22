#include <vector>
#include <chrono>
#include <cmath>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "logger.hpp"
#include "util.hpp"
#include "vulkan.hpp"
#include "renderer.hpp"
#include "act.hpp"
#include "assets.hpp"
#include "model.hpp"
#include "canvas.hpp"

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
    // callbacks
    static auto s_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) -> void;
    static auto s_textInputCallback(GLFWwindow *window, unsigned int codepoint) -> void;
    static auto s_cursorPositionCallback(GLFWwindow *window, double x, double y) -> void;
    static auto s_cursorEnteredCallback(GLFWwindow *window, int entered) -> void;
    static auto s_mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) -> void;
    static auto s_mouseScrollCallback(GLFWwindow *window, double offset_x, double offset_y) -> void;
    static auto s_windowSizeCallback(GLFWwindow *window, int width, int height) -> void;
    static auto s_framebufferSizeCallback(GLFWwindow *window, int width, int height) -> void;

    auto keyCallback(int key, int scancode, int action, int mods) -> void;
    auto textInputCallback(unsigned int codepoint) -> void;
    auto cursorPositionCallback(double x, double y) -> void;
    auto cursorEnteredCallback(int entered) -> void;
    auto mouseButtonCallback(int button, int action, int mods) -> void;
    auto mouseScrollCallback(double offset_x, double offset_y) -> void;
    auto windowSizeCallback(int width, int height) -> void;
    auto framebufferSizeCallback(int width, int height) -> void;

    class ShaderLoaderImpl : public graphics::Renderer::IShaderLoader {
    public:
        ShaderLoaderImpl(const asset::ArchiveReader *archive);
        ~ShaderLoaderImpl() = default;

        auto loadSkinningPassShader() const -> std::optional<std::vector<uint32_t>> override;
        auto loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> override;
        auto loadVectorPassShader() const -> std::optional<std::vector<uint32_t>> override;
        auto loadLightingPassShader() const -> std::optional<std::vector<uint32_t>> override;
        auto loadInterfacePassShader() const -> std::optional<std::vector<uint32_t>> override;

    private:
        const asset::ArchiveReader *archive_ = nullptr;
    };

    auto loadModels() -> util::Result;
    auto loadCubeMesh() -> util::Result;

    ApplicationState() = default;

    std::unique_ptr<asset::MainArchive> main_archive_ = {};

    GLFWwindow *window_handle_ = nullptr;
    std::unique_ptr<graphics::Instance> graphics_instance_ = {};
    std::unique_ptr<graphics::Context> graphics_context_ = {};
    std::unique_ptr<graphics::Renderer> renderer_ = {};

    std::unique_ptr<graphics::Model> test_model_ = {};
    std::optional<graphics::Renderer::MeshId> cube_mesh_ = {};
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

auto inline loadSpvBufferFromFile(const asset::ArchiveReader &archive, std::string_view filename)
    -> std::optional<std::vector<uint32_t>> {
    const auto buffer = archive.getFileContent(filename);
    if (!buffer) {
        LogError("failed to load skinning.spv");
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

auto ApplicationState::ShaderLoaderImpl::loadSkinningPassShader() const -> std::optional<std::vector<uint32_t>> {
    return loadSpvBufferFromFile(*archive_, "skinning.spv");
}

auto ApplicationState::ShaderLoaderImpl::loadGeometryPassShader() const -> std::optional<std::vector<uint32_t>> {
    return loadSpvBufferFromFile(*archive_, "shader.spv");
}

auto ApplicationState::ShaderLoaderImpl::loadVectorPassShader() const -> std::optional<std::vector<uint32_t>> {
    return loadSpvBufferFromFile(*archive_, "vector.spv");
}

auto ApplicationState::ShaderLoaderImpl::loadLightingPassShader() const -> std::optional<std::vector<uint32_t>> {
    return loadSpvBufferFromFile(*archive_, "lighting.spv");
}

auto ApplicationState::ShaderLoaderImpl::loadInterfacePassShader() const -> std::optional<std::vector<uint32_t>> {
    return loadSpvBufferFromFile(*archive_, "hud.spv");
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

    // setup callbacks
    glfwSetKeyCallback(state->window_handle_, ApplicationState::s_keyCallback);
    glfwSetCharCallback(state->window_handle_, ApplicationState::s_textInputCallback);
    glfwSetCursorPosCallback(state->window_handle_, ApplicationState::s_cursorPositionCallback);
    glfwSetCursorEnterCallback(state->window_handle_, ApplicationState::s_cursorEnteredCallback);
    glfwSetMouseButtonCallback(state->window_handle_, ApplicationState::s_mouseButtonCallback);
    glfwSetWindowSizeCallback(state->window_handle_, ApplicationState::s_windowSizeCallback);
    glfwSetFramebufferSizeCallback(state->window_handle_, ApplicationState::s_framebufferSizeCallback);

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

    if (util::Result::eSuccess != state->loadCubeMesh()) {
        LogError("failed to create cube mesh");
        return nullptr;
    }

    if (util::Result::eSuccess != state->loadModels()) {
        LogError("failed to load resources");
        return nullptr;
    }

    return state;
}

auto ApplicationState::loadModels() -> util::Result {
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
    test_model_ = graphics::Model::fromAct(renderer_.get(), model.value());

    if (!test_model_) {
        LogError("failed to load act model");
        return util::Result::eFailure;
    }

    return util::Result::eSuccess;
}

auto ApplicationState::loadCubeMesh() -> util::Result {
    using V = graphics::Renderer::StaticVertex;
    const std::array<V, 24> kCubeVertices = {
        V{1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        V{-1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        V{-1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        V{1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        V{1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        V{1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        V{1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        V{-1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f},
        V{1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{-1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
        V{1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f},
    };

    constexpr std::array<uint32_t, 48> kCubeIndices = {
        0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,  8,  9,  10, 8,  10, 11,
        12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
    };

    cube_mesh_ = renderer_->createMesh(kCubeVertices, kCubeIndices);
    if (!cube_mesh_.has_value()) {
        LogError("failed to upload gpu mesh");
        return util::Result::eFailure;
    }

    return util::Result::eSuccess;
}

auto ApplicationState::run() -> util::Result {
    using Clock = std::chrono::high_resolution_clock;
    constexpr float kMicrosecondsToSeconds = 1e-6;
    constexpr float kCameraRadius = 3.75f;

    auto last_frame = Clock::now();
    float simulation_time = 0.0f;

    // const std::array<glm::fvec3, 7> kCubePositions = {
    //     glm::fvec3{2.0f, 0.0f, 0.0f}, glm::fvec3{0.0f, 2.0f, 0.0f}, glm::fvec3{0.0f, 0.0f, 2.0f},
    //     glm::fvec3{2.0f, 2.0f, 0.0f}, glm::fvec3{2.0f, 0.0f, 2.0f}, glm::fvec3{0.0f, 2.0f, 2.0f},
    //     glm::fvec3{2.0f, 2.0f, 2.0f},
    // };

    const auto animations = test_model_->makeAnimationList();
    auto controller = test_model_->createController(animations[4]);
    auto bind_pose = test_model_->createPose();

    graphics::Canvas::Description canvas_desc = {
        .renderer = renderer_.get(),
    };

    auto canvas = graphics::Canvas::create(canvas_desc);
    auto path = graphics::Canvas::Path(
        glm::fvec4{0.95f, 0.52f, 0.12f, 1.0f}, 30.0f, graphics::Canvas::LineCap::eRound,
        graphics::Canvas::LineJoint::eRound);

    path.appendVertex({100.0f, 100.0f});
    path.appendVertex({500.0f, 200.0f});
    path.appendVertex({700.0f, 100.0f});
    path.appendVertex({800.0f, 500.0f});
    path.appendVertex({1000.0f, 300.0f});
    path.appendVertex({900.0f, 80.0f});
    path.appendVertex({1100.0f, 200.0f});
    path.appendBezier({1300.0f, 400.0f}, {1300.0f, 600.0f}, {1000.0f, 700.0f});
    path.appendVertex({650.0f, 680.0f});
    path.appendVertex({700.0f, 400.0f});
    path.appendVertex({500.0f, 400.0f});
    path.appendVertex({650.0f, 600.0f});
    path.appendVertex({450.0f, 700.0f});
    path.closeContour();

    path.createFill(*canvas);
    path.createStroke(*canvas);

    path.clearContour();
    path.setWidth(10.0f);
    path.setColor(glm::fvec4{1.0f, 0.0f, 0.0f, 1.0f});
    path.appendVertex({300.0f, 300.0f});
    path.appendArc({500.0f, 500.0f}, 3.0f);
    path.createStroke(*canvas);

    while (!glfwWindowShouldClose(window_handle_)) {
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_frame).count();

        const auto delta_time = static_cast<float>(elapsed) * kMicrosecondsToSeconds;
        last_frame = now;
        simulation_time = simulation_time + delta_time;

        controller.integrate(delta_time);

        const glm::fvec3 camera_position = {
            kCameraRadius * ::cosf(simulation_time), kCameraRadius, kCameraRadius * ::sinf(simulation_time)};

        renderer_->camera().setPosition(glm::fvec3{0.0f, 0.0f, 4.0f});

        // test_model_->iterateNodes([&](graphics::Model::NodeId id, [[maybe_unused]] const auto &node) {
        //     const auto &pose_node = *controller.pose().getNode(id);

        //     glm::fmat4x4 s1 = glm::scale(glm::fmat4x4{1.0f}, glm::fvec3{0.0001f, 0.0005f, 0.0001f});
        //     glm::fmat4x4 s2 = glm::scale(glm::fmat4x4{1.0f}, glm::fvec3{3.5f, 3.5f, 3.5f});

        //     glm::fmat4x4 world_matrix = s2 * pose_node.transform() * s1;

        //     graphics::Renderer::OpaqueDrawDescription draw_desc = {
        //         .mesh = cube_mesh_.value(),
        //         .world_matrix = world_matrix,
        //     };

        //     renderer_->drawOpaqueMesh(std::move(draw_desc));
        // });

        // for (const auto &position : kCubePositions) {
        //     graphics::Renderer::OpaqueDrawDescription draw_desc = {
        //         .mesh = cube_mesh_.value(),
        //         .world_matrix = glm::fmat4x4{1.0f},
        //     };

        //     draw_desc.world_matrix =
        //         glm::scale(glm::translate(draw_desc.world_matrix, position), glm::fvec3{0.5f, 0.5f, 0.5f});
        //     renderer_->drawOpaqueMesh(std::move(draw_desc));
        // }

        test_model_->render(
            *renderer_.get(), controller.pose(), glm::scale(glm::fmat4x4{1.0f}, glm::fvec3{3.5f, 3.5f, 3.5f}));

        canvas->draw();

        if (util::Result::eSuccess != renderer_->frame()) {
            LogError("failed to render frame");
            return util::Result::eFailure;
        }

        glfwPollEvents();
    }

    return util::Result::eSuccess;
}

auto ApplicationState::keyCallback(int, int, int, int) -> void {}
auto ApplicationState::textInputCallback(unsigned int) -> void {}
auto ApplicationState::cursorPositionCallback(double, double) -> void {}
auto ApplicationState::cursorEnteredCallback(int) -> void {}
auto ApplicationState::mouseButtonCallback(int, int, int) -> void {}
auto ApplicationState::mouseScrollCallback(double, double) -> void {}

auto ApplicationState::windowSizeCallback([[maybe_unused]] int width, [[maybe_unused]] int height) -> void {
    int32_t window_width = 0, window_height = 0;
    int32_t framebuffer_width = 0, framebuffer_height = 0;

    glfwGetWindowSize(window_handle_, &window_width, &window_height);
    glfwGetFramebufferSize(window_handle_, &framebuffer_width, &framebuffer_height);

    renderer_->resize(
        VkExtent2D{static_cast<uint32_t>(window_width), static_cast<uint32_t>(window_height)},
        VkExtent2D{static_cast<uint32_t>(framebuffer_width), static_cast<uint32_t>(framebuffer_height)});
}

auto ApplicationState::framebufferSizeCallback([[maybe_unused]] int width, [[maybe_unused]] int height) -> void {
    int32_t window_width = 0, window_height = 0;
    int32_t framebuffer_width = 0, framebuffer_height = 0;

    glfwGetWindowSize(window_handle_, &window_width, &window_height);
    glfwGetFramebufferSize(window_handle_, &framebuffer_width, &framebuffer_height);

    renderer_->resize(
        VkExtent2D{static_cast<uint32_t>(window_width), static_cast<uint32_t>(window_height)},
        VkExtent2D{static_cast<uint32_t>(framebuffer_width), static_cast<uint32_t>(framebuffer_height)});
}

ApplicationState::~ApplicationState() noexcept {
    if (window_handle_) {
        glfwDestroyWindow(window_handle_);
        window_handle_ = nullptr;
    }

    glfwTerminate();
}

auto ApplicationState::s_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_keyCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->keyCallback(key, scancode, action, mods);
}

auto ApplicationState::s_textInputCallback(GLFWwindow *window, unsigned int codepoint) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_textInputCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->textInputCallback(codepoint);
}

auto ApplicationState::s_cursorPositionCallback(GLFWwindow *window, double x, double y) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_cursorPositionCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->cursorPositionCallback(x, y);
}

auto ApplicationState::s_cursorEnteredCallback(GLFWwindow *window, int entered) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_cursorEnteredCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->cursorEnteredCallback(entered);
}

auto ApplicationState::s_mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_mouseButtonCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->mouseButtonCallback(button, action, mods);
}

auto ApplicationState::s_mouseScrollCallback(GLFWwindow *window, double offset_x, double offset_y) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_mouseScrollCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->mouseScrollCallback(offset_x, offset_y);
}

auto ApplicationState::s_windowSizeCallback(GLFWwindow *window, int width, int height) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_windowSizeCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->windowSizeCallback(width, height);
}

auto ApplicationState::s_framebufferSizeCallback(GLFWwindow *window, int width, int height) -> void {
    auto ptr = glfwGetWindowUserPointer(window);
    if (!ptr) {
        LogError("invalid window callback s_framebufferSizeCallback");
        return;
    }

    reinterpret_cast<ApplicationState *>(ptr)->framebufferSizeCallback(width, height);
}
