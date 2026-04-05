#include "renderer.hpp"
#include "logger.hpp"

#include "common/byteutils.hpp"

#include <vulkan/vulkan.h>
#include <volk.h>
#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

namespace graphics {

Renderer::Mesh::Mesh(Mesh &&m) noexcept {
    renderer_ = std::move(m.renderer_);
    num_vertices_ = std::move(m.num_vertices_);
    num_indices_ = std::move(m.num_indices_);
    vertex_buffer_ = std::move(m.vertex_buffer_);
    index_bufer_ = std::move(m.index_bufer_);

    m.renderer_ = nullptr;
    m.num_vertices_ = 0;
    m.num_indices_ = 0;
}

auto Renderer::Mesh::operator=(Mesh &&m) noexcept -> Mesh & {
    if (this != &m) {
        renderer_ = std::move(m.renderer_);
        num_vertices_ = std::move(m.num_vertices_);
        num_indices_ = std::move(m.num_indices_);
        vertex_buffer_ = std::move(m.vertex_buffer_);
        index_bufer_ = std::move(m.index_bufer_);

        m.renderer_ = nullptr;
        m.num_vertices_ = 0;
        m.num_indices_ = 0;
    }

    return *this;
}

auto Renderer::Mesh::create(Renderer *renderer, const Description &desc) -> std::optional<Mesh> {
    Mesh mesh;
    mesh.renderer_ = renderer;

    const size_t aligned_buffer_size = util::bytes::align_ptr(desc.num_vertices, kVertexBufferAlign) * desc.vertex_size;
    assert(aligned_buffer_size > desc.vertex_buffer.size_bytes());

    mesh.num_vertices_ = desc.num_vertices;
    mesh.num_indices_ = std::size(desc.indices);

    RendererBuffer::Description vertex_buffer_desc = {
        .usage = desc.vertex_buffer_flags | RendererBuffer::Usage::eVertexBuffer,
        .memory = RendererBuffer::MemoryType::eDevice,
        .size = aligned_buffer_size,
    };

    RendererBuffer::Description index_buffer_desc = {
        .usage = desc.index_buffer_flags | RendererBuffer::Usage::eIndexBuffer,
        .memory = RendererBuffer::MemoryType::eDevice,
        .size = desc.indices.size() * sizeof(uint32_t),
    };

    mesh.vertex_buffer_ = RendererBuffer::create(&renderer->scheduler(), vertex_buffer_desc, desc.vertex_buffer);
    mesh.index_bufer_ = RendererBuffer::create(
        &renderer->scheduler(), index_buffer_desc,
        std::span<const uint8_t>{
            reinterpret_cast<const uint8_t *>(desc.indices.data()), desc.indices.size() * sizeof(uint32_t)});

    return std::move(mesh);
}

auto Renderer::ActorMesh::create(Renderer *renderer, AnimatedMeshId mesh) -> std::optional<ActorMesh> {
    auto *scheduler = &renderer->scheduler();

    const auto *base_mesh = renderer->getAnimMesh(mesh);
    if (!base_mesh) {
        return std::nullopt;
    }

    auto bone_buffer = MutableSharedBuffer<cbSkinningBuffer>::create(scheduler);
    if (!bone_buffer.valid()) {
        return std::nullopt;
    }

    ActorMesh actor{mesh, std::move(bone_buffer)};
    actor.num_vertices_ = util::bytes::align_ptr(base_mesh->numVertices(), kVertexBufferAlign);
    actor.output_buffer_size_ = actor.num_vertices_ * sizeof(StaticVertex);

    RendererBuffer::Description buffer_desc = {
        .usage = RendererBuffer::Usage::eStorageBuffer | RendererBuffer::Usage::eVertexBuffer |
                 RendererBuffer::Usage::eBufferDeviceAddress,
        .memory = RendererBuffer::MemoryType::eDevice,
        .size = actor.output_buffer_size_,
    };

    actor.output_buffer_ = RendererScheduler::MutableBuffer::create(scheduler, buffer_desc);
    return actor;
}

auto Renderer::create(const Description &description) -> std::unique_ptr<Renderer> {
    std::unique_ptr<Renderer> renderer{new (std::nothrow) Renderer()};
    if (!renderer) {
        LogError("vulkan: cannot allocate renderer object");
        return {};
    }

    renderer->context_ = description.context;
    renderer->scheduler_ = description.scheduler;
    renderer->texture_pool_ = BindlessTexturePool<kNumTexturePoolSize>::create(renderer->scheduler_);

    renderer->createRenderTargets();

    renderer->frame_heap_ = MutableSharedBuffer<cbFrameHeapBuffer>::create(renderer->scheduler_);
    if (!renderer->frame_heap_.valid()) {
        LogError("vulkan: renderer failed to create frame heap buffer");
        return nullptr;
    }

    renderer->vector_heap_ = MutableSharedBuffer<cbVectorHeapBuffer>::create(renderer->scheduler_);
    if (!renderer->vector_heap_.valid()) {
        LogError("vulkan: renderer failed to create vector heap buffer");
        return nullptr;
    }

    if (util::Result::eSuccess != renderer->createSkinningPipeline(*description.shader_loader)) {
        LogError("vulkan: renderer cannot initialize compute skinning pipeline");
        return nullptr;
    }

    if (util::Result::eSuccess != renderer->createGeometryPipeline(*description.shader_loader)) {
        LogError("vulkan: renderer cannot initialize graphics pipeline");
        return nullptr;
    }

    if (util::Result::eSuccess != renderer->createVectorPipeline(*description.shader_loader)) {
        LogError("vulkan: renderer cannot initialize vector graphics pipeline");
        return nullptr;
    }

    if (util::Result::eSuccess != renderer->createLightingPipeline(*description.shader_loader)) {
        LogError("vulkan: renderer cannot load lighting pass");
        return nullptr;
    }

    if (util::Result::eSuccess != renderer->createInterfacePipeline(*description.shader_loader)) {
        LogError("vulkan: renderer cannot load interface pass");
        return nullptr;
    }

    return renderer;
}

Renderer::~Renderer() noexcept { LogInfo("vulkan: destroying all renderer resources"); }

auto Renderer::createRgbaTexture(const RendererTexture::RgbaDescription &desc) -> std::optional<TextureId> {
    auto texture = RendererTexture::createFromRgba(scheduler_, desc);
    if (!texture) {
        LogError("vulkan: renderer failed to create rgba texture");
        return std::nullopt;
    }

    return texture_pool_->storeResource(texture);
}

auto Renderer::createSkinningPipeline(IShaderLoader &shader_loader) -> util::Result {
    const auto shader_bytecode = shader_loader.loadSkinningPassShader();
    if (!shader_bytecode.has_value()) {
        LogError("vulkan: renderer cannot load skinning shader");
        return util::Result::eFailure;
    }

    ComputePipelineBuilder builder{scheduler_};

    skinning_pipeline_ = builder
                             .withShaderStage(
                                 shader_bytecode.value(), std::array{ShaderDescription{
                                                              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                              .entry_point = "main",
                                                          }})
                             .withPushConstant<cbSkinningPushConstants>(VK_SHADER_STAGE_COMPUTE_BIT)
                             .build();

    return util::Result::eSuccess;
}

auto Renderer::createGeometryPipeline(IShaderLoader &shader_loader) -> util::Result {
    const auto shader_bytecode = shader_loader.loadGeometryPassShader();
    if (!shader_bytecode.has_value()) {
        LogError("vulkan: renderer cannot load geometry pass shader");
        return util::Result::eFailure;
    }

    RenderPipelineBuilder builder{scheduler_};

    geometry_pipeline_ =
        builder
            .withShaderStage(
                shader_bytecode.value(),
                std::array{
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .entry_point = "vsMain",
                    },
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .entry_point = "fsMain",
                    }})
            .withPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .withPushConstant<cbOpaquePassPushConstants>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .withDescriptorSet(texture_pool_->descriptorLayout())
            .withVertexLayout<StaticVertex>(std::array{
                VertexLayoutElement{0, 0, offsetof(StaticVertex, position), Format::RGB32_FLOAT},
                VertexLayoutElement{1, 0, offsetof(StaticVertex, normal), Format::RGB32_FLOAT},
                VertexLayoutElement{2, 0, offsetof(StaticVertex, uv), Format::RG32_FLOAT},
                VertexLayoutElement{3, 0, offsetof(StaticVertex, color), Format::RGB32_FLOAT},
                VertexLayoutElement{4, 0, offsetof(StaticVertex, tangent), Format::RGBA32_FLOAT},
            })
            .withDepthAttachment(static_cast<Format>(context_->supportedDepthFormat()))
            .addColorAttachment(Format::SRGBA8_UNORM)
            .build();

    return util::Result::eSuccess;
}

auto Renderer::createVectorPipeline(IShaderLoader &shader_loader) -> util::Result {
    const auto shader_bytecode = shader_loader.loadVectorPassShader();
    if (!shader_bytecode.has_value()) {
        LogError("vulkan: renderer cannot load vector pass shader");
        return util::Result::eFailure;
    }

    RenderPipelineBuilder builder{scheduler_};

    vector_pipeline_ =
        builder
            .withShaderStage(
                shader_bytecode.value(),
                std::array{
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .entry_point = "vsMain",
                    },
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .entry_point = "fsMain",
                    }})
            .withCullMode(VK_CULL_MODE_NONE)
            .withPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .withPushConstant<cbVectorPassPushConstants>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .withDescriptorSet(texture_pool_->descriptorLayout())
            .withVertexLayout<VectorVertex>(std::array{
                VertexLayoutElement{0, 0, offsetof(VectorVertex, position), Format::RGB32_FLOAT},
                VertexLayoutElement{1, 0, offsetof(VectorVertex, uv), Format::RG32_FLOAT},
                VertexLayoutElement{2, 0, offsetof(VectorVertex, color), Format::RGBA32_FLOAT},
            })
            .addColorAttachmentAlphaBlend(Format::SRGBA8_UNORM)
            .withSampleCount(context_->chooseBestSampleCount(kNumSamplesForMSAA))
            .build();

    return util::Result::eSuccess;
}

auto Renderer::createLightingPipeline(IShaderLoader &shader_loader) -> util::Result {
    const auto shader_bytecode = shader_loader.loadLightingPassShader();
    if (!shader_bytecode.has_value()) {
        LogError("vulkan: renderer cannot load lighting pass shader");
        return util::Result::eFailure;
    }

    RenderPipelineBuilder builder{scheduler_};

    lighting_pipeline_ =
        builder
            .withShaderStage(
                shader_bytecode.value(),
                std::array{
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .entry_point = "vsMain",
                    },
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .entry_point = "fsMain",
                    }})
            .withCullMode(VK_CULL_MODE_FRONT_BIT)
            .withFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .withPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .withPushConstant<cbLightingPassConstants>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .withDescriptorSet(texture_pool_->descriptorLayout())
            .addColorAttachment(static_cast<Format>(context_->swapchainFormat().format))
            .build();

    return util::Result::eSuccess;
}

auto Renderer::createInterfacePipeline(IShaderLoader &shader_loader) -> util::Result {
    const auto shader_bytecode = shader_loader.loadInterfacePassShader();
    if (!shader_bytecode.has_value()) {
        LogError("vulkan: renderer cannot load interface pass shader");
        return util::Result::eFailure;
    }

    RenderPipelineBuilder builder{scheduler_};

    interface_pipeline_ =
        builder
            .withShaderStage(
                shader_bytecode.value(),
                std::array{
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_VERTEX_BIT,
                        .entry_point = "vsMain",
                    },
                    ShaderDescription{
                        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                        .entry_point = "fsMain",
                    }})
            .withCullMode(VK_CULL_MODE_FRONT_BIT)
            .withFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .withPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .withPushConstant<cbUserInterfacePassConstants>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .withDescriptorSet(texture_pool_->descriptorLayout())
            .addColorAttachmentAlphaBlend(static_cast<Format>(context_->swapchainFormat().format))
            .build();

    return util::Result::eSuccess;
}

auto Renderer::createRenderTargets() -> void {
    const auto surface_extent = context_->surfaceExtent();

    if (depth_buffer_.has_value()) {
        deleteTexture(depth_buffer_.value());
        depth_buffer_ = std::nullopt;
    }

    RendererTexture::Description depth_buffer_desc = {
        .type = RendererTexture::TextureType::eTexture2D,
        .width = surface_extent.width,
        .height = surface_extent.height,
        .depth = 1,
        .array_size = 1,
        .min_filter = RendererTexture::MinFilter::eLinear,
        .mag_filter = RendererTexture::MagFilter::eLinear,
        .usage = RendererTexture::Usage::eUsageDepthAttachment | RendererTexture::Usage::eUsageShaderSample,
        .sample_count = RendererTexture::SampleCount::eSampleCount1,
        .format = static_cast<Format>(context_->supportedDepthFormat()),
    };

    depth_buffer_ = texture_pool_->storeResource(RendererTexture::create(scheduler_, depth_buffer_desc));

    RendererTexture::Description msaa_target_desc = {
        .type = RendererTexture::TextureType::eTexture2D,
        .width = surface_extent.width,
        .height = surface_extent.height,
        .min_filter = RendererTexture::MinFilter::eLinear,
        .mag_filter = RendererTexture::MagFilter::eLinear,
        .usage = RendererTexture::Usage::eUsageColorAttachment | RendererTexture::Usage::eUsageShaderSample,
        .sample_count = RendererTexture::SampleCount::eSampleCount4,
        .format = Format::SRGBA8_UNORM,
    };

    RendererTexture::Description color_target_desc = {
        .type = RendererTexture::TextureType::eTexture2D,
        .width = surface_extent.width,
        .height = surface_extent.height,
        .min_filter = RendererTexture::MinFilter::eLinear,
        .mag_filter = RendererTexture::MagFilter::eLinear,
        .usage = RendererTexture::Usage::eUsageColorAttachment | RendererTexture::Usage::eUsageShaderSample,
        .sample_count = RendererTexture::SampleCount::eSampleCount1,
        .format = Format::SRGBA8_UNORM,
    };

    for (uint32_t i = 0; i < RendererScheduler::kNumFramesInFlight; ++i) {
        if (per_frame_data_[i].geometry_target.has_value()) {
            deleteTexture(per_frame_data_[i].geometry_target.value());
        }

        if (per_frame_data_[i].vector_target.has_value()) {
            deleteTexture(per_frame_data_[i].vector_target.value());
        }

        if (per_frame_data_[i].vector_target_msaa.has_value()) {
            deleteTexture(per_frame_data_[i].vector_target_msaa.value());
        }

        if (auto geometry_target = RendererTexture::create(scheduler_, color_target_desc)) {
            per_frame_data_[i].geometry_target = texture_pool_->storeResource(geometry_target);
        }

        if (auto vector_target = RendererTexture::create(scheduler_, color_target_desc)) {
            per_frame_data_[i].vector_target = texture_pool_->storeResource(vector_target);
        }

        if (auto vector_target_msaa = RendererTexture::create(scheduler_, msaa_target_desc)) {
            per_frame_data_[i].vector_target_msaa = texture_pool_->storeResource(vector_target_msaa);
        }
    }
}

auto Renderer::frame() -> util::Result {
    if (scheduler_->swapchainOutOfDate()) {
        if (!pending_resize_.has_value()) {
            return util::Result::eSuccess;
        }

        LogInfo("vulkan: renderer will trigger swapchain resize");

        scheduler_->resizeSwapchain(pending_resize_->surface_extent, pending_resize_->framebuffer_extent);
        createRenderTargets();

        pending_resize_.reset();
    }

    camera_.setAspect(
        static_cast<float>(context_->framebufferExtent().width) /
        static_cast<float>(context_->framebufferExtent().height));

    scheduler_->frame([&](const RendererScheduler::FrameContext &context) { scheduleFrameWork(context); });
    return util::Result::eSuccess;
}

auto Renderer::useTextureHandle(const RendererScheduler::FrameContext &context, std::optional<TextureId> handle)
    -> int32_t {
    if (!handle.has_value()) {
        return -1;
    }

    auto texture = texture_pool_->getResource(handle.value());
    if (!texture) {
        return -1;
    }

    (void)scheduler_->use(context, texture);
    return handle->index();
}

auto Renderer::prepareIndexedDraws(const RendererScheduler::FrameContext &context, OpaqueIndexedDrawList &draw_list)
    -> void {
    auto &scene_buffer_data = frame_heap_.data();

    for (uint32_t i = 0; i < draw_queue_.getFill() && !draw_list.full(); ++i) {
        const auto object_id = draw_list.getFill();

        scene_buffer_data.static_objects[object_id].world = draw_queue_[i]->world_matrix;
        scene_buffer_data.static_objects[object_id].diffuse_map =
            useTextureHandle(context, draw_queue_[i]->diffuse_map);
        scene_buffer_data.static_objects[object_id].normal_map = useTextureHandle(context, draw_queue_[i]->normal_map);

        auto *mesh = mesh_pool_.get(draw_queue_[i]->mesh);

        draw_list.push(
            IndexedDrawCall{
                .vertex_buffer = scheduler_->use(context, mesh->vertexBuffer()).nativeBuffer(),
                .index_buffer = scheduler_->use(context, mesh->indexBuffer()).nativeBuffer(),
                .num_indices = mesh->numIndices(),
            });
    }

    for (uint32_t i = 0; i < skinning_queue_.getFill() && !draw_list.full(); ++i) {
        const auto object_id = draw_list.getFill();

        scene_buffer_data.static_objects[object_id].world = skinning_queue_[i]->world_matrix;
        scene_buffer_data.static_objects[object_id].diffuse_map =
            useTextureHandle(context, skinning_queue_[i]->diffuse_map);
        scene_buffer_data.static_objects[object_id].normal_map =
            useTextureHandle(context, skinning_queue_[i]->normal_map);

        const auto actor_id = skinning_queue_[i]->skinned_mesh;
        auto *actor = actor_mesh_pool_.get(actor_id);
        auto *mesh = anim_mesh_pool_.get(actor->inputMesh());

        if (!actor || !mesh) {
            continue;
        }

        actor->transformBuffer().upload();

        draw_list.push(
            IndexedDrawCall{
                .vertex_buffer = scheduler_->use(context, actor->vertexBuffer()).nativeBuffer(),
                .index_buffer = scheduler_->use(context, mesh->indexBuffer()).nativeBuffer(),
                .num_indices = mesh->numIndices(),
            });
    }
}

auto Renderer::scheduleFrameWork(const RendererScheduler::FrameContext &context) -> void {
    auto &current_frame_data = per_frame_data_[context.getCurrentFrameIndex()];

    const auto &skinning_pipeline = scheduler_->use(context, skinning_pipeline_);
    const auto &geometry_pipeline = scheduler_->use(context, geometry_pipeline_);
    const auto &vector_pipeline = scheduler_->use(context, vector_pipeline_);
    const auto &lighting_pipeline = scheduler_->use(context, lighting_pipeline_);
    const auto &interface_pipeline = scheduler_->use(context, interface_pipeline_);

    // this frame is not executing, so we can touch its descriptor set
    texture_pool_->updateDescriptorSet();

    // update frame data
    auto &scene_buffer = scheduler_->use(context, frame_heap_.buffer());
    auto &scene_buffer_data = frame_heap_.data();

    scene_buffer_data.projection = camera_.projection();
    scene_buffer_data.projection_inv = camera_.projectionInv();
    scene_buffer_data.view = camera_.view();
    scene_buffer_data.view_inv = camera_.viewInv();

    // opaque mesh rendering
    OpaqueIndexedDrawList indexed_draws;
    prepareIndexedDraws(context, indexed_draws);

    // vector graphics rendering
    auto &vector_buffer = scheduler_->use(context, vector_heap_.buffer());
    auto &vector_buffer_data = vector_heap_.data();

    const auto fwidth = static_cast<float>(context_->framebufferExtent().width);
    const auto fheight = static_cast<float>(context_->framebufferExtent().height);
    const auto iw = 2.0f / fwidth;
    const auto ih = 2.0f / fheight;

    // clang-format off
    vector_buffer_data.view_projection = {
        iw, 0.0f, 0.0f, 0.0f,
        0.0f, -ih, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    // clang-format on

    for (uint32_t i = 0; i < vector_queue_.getFill(); ++i) {
        vector_buffer_data.vector_objects[i].world = vector_queue_[i]->world_matrix;
        vector_buffer_data.vector_objects[i].diffuse_map = useTextureHandle(context, vector_queue_[i]->diffuse_map);
    }

    auto command_buffer = scheduler_->commandBufffer(context);

    // invoke compute shader skinning
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, skinning_pipeline.nativePipeline());

    cbSkinningPushConstants skinning_constants;

    for (uint32_t i = 0; i < skinning_queue_.getFill(); ++i) {
        const auto actor_id = skinning_queue_[i]->skinned_mesh;
        auto *actor = actor_mesh_pool_.get(actor_id);
        auto *input_mesh = anim_mesh_pool_.get(actor->inputMesh());

        // pass the buffers using push constants
        skinning_constants.input_buffer = scheduler_->use(context, input_mesh->vertexBuffer()).deviceAddress();
        skinning_constants.output_buffer = scheduler_->use(context, actor->vertexBuffer()).deviceAddress();
        skinning_constants.bone_buffer = scheduler_->use(context, actor->transformBuffer().buffer()).deviceAddress();

        const auto num_dispatches = (input_mesh->num_vertices_ + kVertexBufferAlign - 1) / kVertexBufferAlign;

        vkCmdPushConstants(
            command_buffer, skinning_pipeline.nativePipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
            sizeof(cbSkinningPushConstants), &skinning_constants);
        vkCmdDispatch(command_buffer, num_dispatches, 1, 1);
    }

    // upload buffer data
    frame_heap_.upload();
    vector_heap_.upload();

    // it is always used in the frame
    texture_pool_->use(context);

    auto &rc_geometry_target =
        scheduler_->use(context, texture_pool_->getResource(current_frame_data.geometry_target.value()));
    auto &rc_vector_target =
        scheduler_->use(context, texture_pool_->getResource(current_frame_data.vector_target.value()));
    auto &rc_vector_target_msaa =
        scheduler_->use(context, texture_pool_->getResource(current_frame_data.vector_target_msaa.value()));

    VkImage geometry_target_image = rc_geometry_target.nativeImage();
    VkImage vector_target_image = rc_vector_target.nativeImage();
    VkImage vector_target_msaa_image = rc_vector_target_msaa.nativeImage();

    VkImageView geometry_target_view = rc_geometry_target.nativeView();
    VkImageView vector_target_view = rc_vector_target.nativeView();
    VkImageView vector_target_msaa_view = rc_vector_target_msaa.nativeView();

    VkMemoryBarrier2 compute_mem_barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
    };

    auto &depth_buffer = scheduler_->use(context, texture_pool_->getResource(depth_buffer_.value()));
    const auto native_depth_image = depth_buffer.nativeImage();
    const auto native_depth_view = depth_buffer.nativeView();

    std::array<VkImageMemoryBarrier2, 4> output_barriers = {
        // depth buffer
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = native_depth_image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
        },

        // geometry buffer
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = geometry_target_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },

        // vector graphics buffer
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = vector_target_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },

        // vector graphics buffer msaa
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = vector_target_msaa_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },
    };

    VkDependencyInfo render_dependency_desc = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &compute_mem_barrier,
        .imageMemoryBarrierCount = static_cast<uint32_t>(output_barriers.size()),
        .pImageMemoryBarriers = output_barriers.data(),
    };

    vkCmdPipelineBarrier2(command_buffer, &render_dependency_desc);

    VkRenderingAttachmentInfo color_attachment_desc = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = geometry_target_view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.207f, 0.36f, 0.64f, 1.0f}}},
    };

    VkRenderingAttachmentInfo depth_attachment_desc = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = native_depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}},
    };

    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea =
            {
                .extent = context_->framebufferExtent(),
            },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_desc,
        .pDepthAttachment = &depth_attachment_desc,
    };

    vkCmdBeginRendering(command_buffer, &rendering_info);

    // VK_HR_maintenance1, core from Vulkan 1.1, we can flip y to be "opengl-friendly"
    VkViewport vp = {
        .x = 0.0f,
        .y = static_cast<float>(context_->surfaceExtent().height),
        .width = static_cast<float>(context_->surfaceExtent().width),
        .height = -static_cast<float>(context_->surfaceExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {.extent = context_->surfaceExtent()};

    vkCmdSetViewport(command_buffer, 0, 1, &vp);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline.nativePipeline());

    {
        VkDescriptorSet ds = texture_pool_->descriptorSet();
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, geometry_pipeline.nativePipelineLayout(), 0, 1, &ds, 0,
            nullptr);

        VkDeviceSize vertex_offset = 0;
        cbOpaquePassPushConstants push_constants;

        for (uint32_t i = 0; i < indexed_draws.getFill(); ++i) {
            const auto &draw_call = indexed_draws[i];

            vkCmdBindVertexBuffers(command_buffer, 0, 1, &draw_call->vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(command_buffer, draw_call->index_buffer, 0, VK_INDEX_TYPE_UINT32);

            push_constants.frame_heap = scene_buffer.deviceAddress();
            push_constants.object_id = i;

            vkCmdPushConstants(
                command_buffer, geometry_pipeline.nativePipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cbOpaquePassPushConstants),
                &push_constants);

            vkCmdDrawIndexed(command_buffer, draw_call->num_indices, 1, 0, 0, 0);
        }
    }

    vkCmdEndRendering(command_buffer);

    // vector graphics rendering
    VkRenderingAttachmentInfo vector_color_attachment_desc = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = vector_target_msaa_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView = vector_target_view,
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}},
    };

    VkRenderingInfo vector_rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea =
            {
                .extent = context_->framebufferExtent(),
            },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &vector_color_attachment_desc,
        .pDepthAttachment = nullptr,
    };

    vkCmdBeginRendering(command_buffer, &vector_rendering_info);

    vkCmdSetViewport(command_buffer, 0, 1, &vp);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_pipeline.nativePipeline());

    {
        VkDescriptorSet ds = texture_pool_->descriptorSet();
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_pipeline.nativePipelineLayout(), 0, 1, &ds, 0,
            nullptr);

        VkDeviceSize vertex_offset = 0;
        cbVectorPassPushConstants push_constants;

        for (uint32_t i = 0; i < vector_queue_.getFill(); ++i) {
            const auto &draw_call = vector_queue_[i];
            const auto vector_mesh = vector_mesh_pool_.get(draw_call->vector_mesh);

            if (!vector_mesh) {
                continue;
            }

            vkCmdBindVertexBuffers(
                command_buffer, 0, 1, scheduler_->use(context, vector_mesh->vertexBuffer()).addrOf(), &vertex_offset);
            vkCmdBindIndexBuffer(
                command_buffer, scheduler_->use(context, vector_mesh->indexBuffer()).nativeBuffer(), 0,
                VK_INDEX_TYPE_UINT32);

            push_constants.frame_heap = vector_buffer.deviceAddress();
            push_constants.object_id = i;

            vkCmdPushConstants(
                command_buffer, vector_pipeline.nativePipelineLayout(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cbVectorPassPushConstants),
                &push_constants);

            vkCmdDrawIndexed(command_buffer, vector_mesh->numIndices(), 1, 0, 0, 0);
        }
    }

    vkCmdEndRendering(command_buffer);

    // barriers before final compositing pass
    std::array<VkImageMemoryBarrier2, 3> compositing_barriers = {
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .image = context_->swapchainImages()[context.getCurrentImageIndex()],
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },

        // geometry buffer
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = geometry_target_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },

        // vector graphics buffer
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_RESOLVE_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = vector_target_image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        },
    };

    VkDependencyInfo composite_dependency_desc = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = static_cast<uint32_t>(compositing_barriers.size()),
        .pImageMemoryBarriers = compositing_barriers.data(),
    };

    vkCmdPipelineBarrier2(command_buffer, &composite_dependency_desc);

    // composite vector graphics and geometry buffers
    // vector graphics rendering
    VkRenderingAttachmentInfo swap_chain_rendering_attachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = context_->swapchainImageViews()[context.getCurrentImageIndex()],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}},
    };

    VkRenderingInfo compositing_rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea =
            {
                .extent = context_->framebufferExtent(),
            },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &swap_chain_rendering_attachment,
        .pDepthAttachment = nullptr,
    };

    vkCmdBeginRendering(command_buffer, &compositing_rendering_info);

    VkViewport vp_fullscreen = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(context_->surfaceExtent().width),
        .height = static_cast<float>(context_->surfaceExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    vkCmdSetViewport(command_buffer, 0, 1, &vp_fullscreen);
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting_pipeline.nativePipeline());
        VkDescriptorSet ds = texture_pool_->descriptorSet();
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lighting_pipeline.nativePipelineLayout(), 0, 1, &ds, 0,
            nullptr);

        cbLightingPassConstants push_constants;
        push_constants.texture = useTextureHandle(context, current_frame_data.geometry_target);

        vkCmdPushConstants(
            command_buffer, lighting_pipeline.nativePipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cbLightingPassConstants),
            &push_constants);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);
    }

    {
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, interface_pipeline.nativePipeline());
        VkDescriptorSet ds = texture_pool_->descriptorSet();
        vkCmdBindDescriptorSets(
            command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, interface_pipeline.nativePipelineLayout(), 0, 1, &ds, 0,
            nullptr);

        cbUserInterfacePassConstants push_constants;
        push_constants.texture = useTextureHandle(context, current_frame_data.vector_target);

        vkCmdPushConstants(
            command_buffer, interface_pipeline.nativePipelineLayout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cbUserInterfacePassConstants),
            &push_constants);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);
    }

    vkCmdEndRendering(command_buffer);

    VkImageMemoryBarrier2 barrier_present = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = context_->swapchainImages()[context.getCurrentImageIndex()],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };

    VkDependencyInfo dependency_present = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier_present,
    };

    vkCmdPipelineBarrier2(command_buffer, &dependency_present);

    skinning_queue_.clear();
    vector_queue_.clear();
    draw_queue_.clear();
}

} // namespace graphics
