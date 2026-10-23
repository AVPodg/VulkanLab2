#include "application.hpp"
#include "graphics.hpp"
#include "scene.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstddef>
#include <iostream>

namespace application {
namespace {
scene::Settings settings;
graphics::Buffer vertices, indices;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE; 
VkPipeline pipeline = VK_NULL_HANDLE; 
VkPhysicalDeviceProperties gpu{}; 
double lastTime = -1.0; 
float panelWidth = 350.f; 
struct PushConstants { glm::mat4 mvp; glm::vec4 tint; };
static_assert(sizeof(PushConstants) == 80 && offsetof(PushConstants, tint) == 64); 

void createPipeline(const std::filesystem::path& directory) { 
    const auto device = graphics::internal::context.device;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    try {
        vert = graphics::loadShader(directory / "pyramid.vert.spv");
        frag = graphics::loadShader(directory / "pyramid.frag.spv");

        const VkPipelineShaderStageCreateInfo stages[]{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"}
        };
        
        const VkVertexInputBindingDescription binding{0, sizeof(scene::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[]{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, color)}
        };
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 2;
        input.pVertexAttributeDescriptions = attributes;
        
        // сборка треугольников
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; 
        
        // область отображения
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; 
        viewport.viewportCount = viewport.scissorCount = 1;
        
        // превращение треугольников во фрагменты
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; 
        raster.cullMode = VK_CULL_MODE_BACK_BIT; 
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; 
        raster.lineWidth = 1.f; 
        
        // сглаживание
        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; 
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; 
        
        // проверка глубины
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE; 
        depth.depthCompareOp = VK_COMPARE_OP_LESS; 
        
        // запись цвета
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 0xf; 
        
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1; 
        blend.pAttachments = &attachment; 
        
        // динамические настройки
        // Здесь мы сообщаем Vulkan, что viewport и scissor будут задаваться динамически.
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        
        // Push Constants — передача MVP и tint
        const VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)}; 
        
        // Создание Pipeline Layout
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        // Здесь указываем, что наш Pipeline Layout содержит один диапазон push constants.
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        
        // Собираем Graphics Pipeline
        graphics::check(vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages; 
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples; 
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend; 
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout; 
        info.renderPass = graphics::internal::context.render_pass; 

        // Создание конвейера Vulkan
        graphics::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), // vkCreateGraphicsPipelines() создаёт один или несколько графических конвейеров.
                        "vkCreateGraphicsPipelines");
    } catch (...) { // Обработка ошибок и освобождение шейдеров
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, vert, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
}
}

bool initialize(const std::filesystem::path& shaderDirectory) { 
    try {
        // Создание вершин и буферов Vulkan
        const auto data = scene::makeVertices(); 
        vertices = graphics::createBuffer(data.data(), sizeof(data), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indices = graphics::createBuffer(scene::indices.data(), sizeof(scene::indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        
        createPipeline(shaderDirectory); 
        vkGetPhysicalDeviceProperties(graphics::internal::context.physical_device, &gpu); 
        
        // Настройка ImGui
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle(); 
        style.WindowPadding = {16.f, 14.f};
        style.FramePadding = {7.f, 4.f}; 
        style.ItemSpacing = {7.f, 7.f};
        style.FrameRounding = style.GrabRounding = 4.f; 
        style.Colors[ImGuiCol_WindowBg] = {0.055f, 0.065f, 0.09f, 1.f};
        style.Colors[ImGuiCol_Header] = {0.12f, 0.20f, 0.30f, 1.f};
        style.Colors[ImGuiCol_Button] = {0.12f, 0.30f, 0.48f, 1.f};
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Application initialization: " << e.what() << '\n';
        shutdown(); 
        return false;
    }
}

void shutdown() {
    const auto device = graphics::internal::context.device;
    vkDeviceWaitIdle(device); 
    vkDestroyPipeline(device, pipeline, nullptr); 
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr); 
    pipeline = VK_NULL_HANDLE; 
    pipelineLayout = VK_NULL_HANDLE;
    graphics::destroyBuffer(indices); 
    graphics::destroyBuffer(vertices);
}

void update(double time) { 
    scene::advance(settings, lastTime < 0.0 ? 0.0 : time - lastTime); 
    lastTime = time; 
    const auto& io = ImGui::GetIO(); 
    panelWidth = std::min(350.f, io.DisplaySize.x * 0.47f);
    ImGui::SetNextWindowPos({0.f, 0.f}); 
    ImGui::SetNextWindowSize({panelWidth, io.DisplaySize.y}); 
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | 
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored({0.4f, 0.75f, 1.f, 1.f}, "COMPUTER GRAPHICS / LAB 01");
    ImGui::Text("PYRAMID"); 
    ImGui::TextDisabled("Vulkan / C++ / variant 6 / grade 4");
    ImGui::Separator(); 
    ImGui::PushItemWidth(-92.f); 
    if (ImGui::CollapsingHeader("1. Projection", ImGuiTreeNodeFlags_DefaultOpen)) { 
        if (ImGui::RadioButton("Perspective", settings.perspective)) settings.perspective = true; 
        ImGui::SameLine(); 
        if (ImGui::RadioButton("Orthographic", !settings.perspective)) settings.perspective = false;
        if (settings.perspective) ImGui::SliderFloat("FOV", &settings.fov, 20.f, 80.f, "%.0f deg");
        else ImGui::SliderFloat("View height", &settings.orthoHeight, 2.f, 20.f, "%.1f"); 
    }
    if (ImGui::CollapsingHeader("2. Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &settings.position.x, 0.03f, -8.f, 8.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat3("Rotation", &settings.rotation.x, 0.5f, -180.f, 180.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragFloat3("Scale", &settings.scale.x, 0.01f, 0.1f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("X / Y / Z; rotation in degrees");
    }
    if (ImGui::CollapsingHeader("3. Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable trajectory", &settings.animate);
        if (ImGui::Button(settings.playing ? "Pause" : "Play", {80.f, 0.f})) settings.playing = !settings.playing;
        ImGui::SameLine();
        if (ImGui::Button("Restart")) settings.animationTime = 0.0;
        ImGui::SliderFloat("Speed", &settings.speed, 0.1f, 3.f, "%.2fx");
        ImGui::SliderFloat("Radius", &settings.radius, 0.f, 3.f, "%.2f");
        ImGui::SliderFloat("Height", &settings.height, 0.f, 2.f, "%.2f");
        ImGui::SliderFloat("Spin", &settings.spin, -180.f, 180.f, "%.0f deg/s");
        ImGui::TextDisabled("3D figure-eight | time %.2f s", settings.animationTime);
    }
    if (ImGui::CollapsingHeader("4-5. Vertex color x tint", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Tint", &settings.tint.x);
        if (ImGui::SmallButton("White tint")) settings.tint = glm::vec3(1.f);
        ImGui::TextWrapped("Vertex RGB comes from local XYZ. Tint multiplies the vertex colors.");
    }
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::SliderFloat("Yaw", &settings.cameraYaw, -180.f, 180.f, "%.0f");
        ImGui::SliderFloat("Elevation", &settings.cameraElevation, -80.f, 80.f, "%.0f");
        ImGui::SliderFloat("Distance", &settings.cameraDistance, 3.f, 25.f, "%.1f");
    }
    ImGui::PopItemWidth();
    if (ImGui::Button("Reset all", {-1.f, 0.f})) settings = {};
    ImGui::TextWrapped("RMB drag: orbit | Wheel: zoom\nSpace: pause/play | R: reset | Esc: exit");
    ImGui::Separator();
    ImGui::TextWrapped("GPU: %s", gpu.deviceName);
    ImGui::TextDisabled("5 vertices / 6 triangles | %.0f FPS", io.Framerate);
    ImGui::End();
    if (!io.WantCaptureMouse && io.MousePos.x > panelWidth) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            settings.cameraYaw += io.MouseDelta.x * 0.35f;
            settings.cameraElevation = std::clamp(settings.cameraElevation + io.MouseDelta.y * 0.35f, -80.f, 80.f);
        }
        if (settings.perspective)
            settings.cameraDistance = std::clamp(settings.cameraDistance - io.MouseWheel * 0.5f, 3.f, 25.f);
        else settings.orthoHeight = std::clamp(settings.orthoHeight - io.MouseWheel * 0.5f, 2.f, 20.f);
    }
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) settings.playing = !settings.playing;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) settings = {};
    }
}

void render(const graphics::internal::FrameData& fd) { 
    const auto& c = graphics::internal::context; 
    graphics::check(vkResetCommandBuffer(fd.command_buffer, 0), "vkResetCommandBuffer");
    
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; 
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    graphics::check(vkBeginCommandBuffer(fd.command_buffer, &begin), "vkBeginCommandBuffer"); 
    
    VkClearValue clear[2]{};
    clear[0].color = {{0.025f, 0.035f, 0.055f, 1.f}};
    clear[1].depthStencil = {1.f, 0};
    
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; 
    pass.renderPass = c.render_pass; 
    pass.framebuffer = fd.framebuffer; 
    pass.renderArea.extent = c.swapchain_extent; 
    pass.clearValueCount = 2; 
    pass.pClearValues = clear;
    
    // Начало Render Pass
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const auto& io = ImGui::GetIO(); 
    const uint32_t left = std::min(c.swapchain_extent.width - 1, 
        static_cast<uint32_t>(panelWidth * io.DisplayFramebufferScale.x));
    const uint32_t width = c.swapchain_extent.width - left; 
    
    const VkViewport viewport{static_cast<float>(left), 0.f, static_cast<float>(width), 
                              static_cast<float>(c.swapchain_extent.height), 0.f, 1.f};
    const VkRect2D scissor{{static_cast<int32_t>(left), 0}, {width, c.swapchain_extent.height}}; 
    
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    
    // Подключение Graphics Pipeline
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    
    // Подключение Vertex Buffer
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertices.handle, &offset);
    
    // Подключение Index Buffer
    vkCmdBindIndexBuffer(fd.command_buffer, indices.handle, 0, VK_INDEX_TYPE_UINT16);
    
    // Вычисление aspect ratio
    const float aspect = static_cast<float>(width) / static_cast<float>(c.swapchain_extent.height);
    
    // Создание Push Constants
    const PushConstants push{scene::projection(settings, aspect) * scene::view(settings) * scene::model(settings),
                             glm::vec4(settings.tint, 1.f)};
    vkCmdPushConstants(fd.command_buffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(scene::indices.size()), 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    graphics::check(vkEndCommandBuffer(fd.command_buffer), "vkEndCommandBuffer");
}

}
