#include "lighting_renderer.hpp"
#include "graphics.hpp"
#include <imgui.h>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace lighting {
namespace {
struct SceneUniform {
    glm::mat4 viewProjection;
    glm::vec4 camera, ambient, direction, directionalColor;
    glm::ivec4 counts;
};
struct ModelUniform {
    glm::mat4 transform, normalMatrix;
    glm::vec4 albedo, specular, parameters;
};
struct PointData { glm::vec4 position, color; };
struct SpotData { glm::vec4 position, direction, color, angles; };
static_assert(sizeof(SceneUniform) == 144 && offsetof(SceneUniform, counts) == 128);
static_assert(sizeof(ModelUniform) == 176 && offsetof(ModelUniform, parameters) == 160);
static_assert(sizeof(PointData) == 32 && sizeof(SpotData) == 64);
struct DynamicBuffer { graphics::Buffer buffer; size_t capacity = 0; };
DynamicBuffer sceneBuffer, pointBuffer, spotBuffer, modelBuffer, cubeBuffer, lineBuffer;
graphics::Buffer pyramidBuffer;
uint32_t pyramidCount = 0;
VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE, modelLayout = VK_NULL_HANDLE;
VkPipelineLayout lightingLayout = VK_NULL_HANDLE, markerLayout = VK_NULL_HANDLE;
VkPipeline litPipeline = VK_NULL_HANDLE, cubePipeline = VK_NULL_HANDLE, linePipeline = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet sceneSet = VK_NULL_HANDLE, modelSet = VK_NULL_HANDLE;

void upload(DynamicBuffer& b, const void* data, size_t size, VkBufferUsageFlags usage) {
    size_t needed = std::max(size, size_t(16));
    if (needed > b.capacity) {
        graphics::destroyBuffer(b.buffer);
        b.capacity = std::max(needed, b.capacity * 2);
        std::vector<unsigned char> zero(b.capacity, 0);
        b.buffer = graphics::createBuffer(zero.data(), zero.size(), usage);
    }
    if (size == 0) return;
    void* mapped = nullptr;
    auto allocator = graphics::internal::context.allocator;
    graphics::check(vmaMapMemory(allocator, b.buffer.allocation, &mapped), "vmaMapMemory");
    std::memcpy(mapped, data, size);
    VkResult result = vmaFlushAllocation(allocator, b.buffer.allocation, 0, size);
    vmaUnmapMemory(allocator, b.buffer.allocation);
    graphics::check(result, "vmaFlushAllocation");
}
void bindBuffer(VkDescriptorSet set, uint32_t binding, const DynamicBuffer& buffer, VkDescriptorType type) {
    VkDescriptorBufferInfo info{buffer.buffer.handle, 0, buffer.capacity};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set; write.dstBinding = binding;
    write.descriptorCount = 1; write.descriptorType = type; write.pBufferInfo = &info;
    vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

VkPipeline createPipeline(const std::filesystem::path& directory, bool marker, bool lines) {
    const auto device = graphics::internal::context.device;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    try {
        vert = graphics::loadShader(directory / (marker ? "marker.vert.spv" : "lighting.vert.spv"));
        frag = graphics::loadShader(directory / (marker ? "marker.frag.spv" : "lighting.frag.spv"));

        const VkPipelineShaderStageCreateInfo stages[]{
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert, .pName = "main"},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main"}
        };

        const VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const VkVertexInputAttributeDescription attributes[]{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}
        };
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = marker ? 2 : 3;
        input.pVertexAttributeDescriptions = attributes;


        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;


        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;


        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = marker ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.f;


        VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;


        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;


        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = 0xf;

        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;



        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;

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
        info.layout = marker ? markerLayout : lightingLayout;
        info.renderPass = graphics::internal::context.render_pass;


        graphics::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                        "vkCreateGraphicsPipelines");
    } catch (...) {
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, vert, nullptr);
        throw;
    }
    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);
    return pipeline;
}

void createDescriptors() {
    auto device = graphics::internal::context.device;
    const VkShaderStageFlags both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, both, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 3; layout.pBindings = bindings;
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &sceneLayout), "scene layout");
    layout.bindingCount = 1;
    graphics::check(vkCreateDescriptorSetLayout(device, &layout, nullptr, &modelLayout), "model layout");
    VkDescriptorSetLayout sets[] = {sceneLayout, modelLayout};
    VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayout.setLayoutCount = 2; pipelineLayout.pSetLayouts = sets;
    graphics::check(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &lightingLayout), "lighting layout");
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)};
    pipelineLayout.setLayoutCount = 0; pipelineLayout.pSetLayouts = nullptr;
    pipelineLayout.pushConstantRangeCount = 1; pipelineLayout.pPushConstantRanges = &push;
    graphics::check(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &markerLayout), "marker layout");
    VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
    graphics::check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptorPool), "descriptor pool");
    VkDescriptorSetLayout layouts[] = {sceneLayout, modelLayout};
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool; alloc.descriptorSetCount = 2; alloc.pSetLayouts = layouts;
    VkDescriptorSet allocated[2]{};
    graphics::check(vkAllocateDescriptorSets(device, &alloc, allocated), "descriptor sets");
    sceneSet = allocated[0]; modelSet = allocated[1];
}
void uploadModel(const glm::mat4& transform, const Material& material) {
    ModelUniform data{transform, glm::transpose(glm::inverse(transform)),
        glm::vec4(material.albedo, 1), glm::vec4(material.specular, 1),
        {material.shininess, material.vertexColors ? 1.f : 0.f, 0, 0}};
    upload(modelBuffer, &data, sizeof(data), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    bindBuffer(modelSet, 0, modelBuffer, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
}
void drawMesh(VkCommandBuffer cmd, const graphics::Buffer& buffer, uint32_t count) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer.handle, &offset);
    vkCmdDraw(cmd, count, 1, 0, 0);
}
}
void initializeRenderer(const std::filesystem::path& directory) {
    auto p = pyramid();
    pyramidCount = static_cast<uint32_t>(p.size());
    pyramidBuffer = graphics::createBuffer(p.data(), p.size()*sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    createDescriptors();
    litPipeline = createPipeline(directory, false, false);
    cubePipeline = createPipeline(directory, true, false);
    linePipeline = createPipeline(directory, true, true);
}
void shutdownRenderer() {
    auto device = graphics::internal::context.device;
    vkDeviceWaitIdle(device);
    for (auto p : {litPipeline, cubePipeline, linePipeline}) vkDestroyPipeline(device, p, nullptr);
    vkDestroyPipelineLayout(device, lightingLayout, nullptr);
    vkDestroyPipelineLayout(device, markerLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, sceneLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, modelLayout, nullptr);
    for (auto b : {&sceneBuffer, &pointBuffer, &spotBuffer, &modelBuffer, &cubeBuffer, &lineBuffer}) {
        graphics::destroyBuffer(b->buffer); b->capacity = 0;
    }
    graphics::destroyBuffer(pyramidBuffer);
}
void render(const graphics::internal::FrameData& frame, const State& s, float panelWidth) {
    const auto& c = graphics::internal::context;
    const auto& io = ImGui::GetIO();
    uint32_t left = std::min(c.swapchain_extent.width-1, static_cast<uint32_t>(panelWidth*io.DisplayFramebufferScale.x));
    uint32_t width = c.swapchain_extent.width-left;
    glm::mat4 vp = projection(s.camera, float(width)/float(c.swapchain_extent.height))*view(s.camera);
    std::vector<PointData> points;
    std::vector<SpotData> spots;
    for (const auto& p : s.points) if (p.enabled)
        points.push_back({glm::vec4(p.position,1), glm::vec4(p.color,p.intensity)});
    for (const auto& p : s.spots) if (p.enabled)
        spots.push_back({glm::vec4(p.position,1), glm::vec4(safeDirection(p.direction),0), glm::vec4(p.color,p.intensity),
            {std::cos(glm::radians(p.innerAngle)),std::cos(glm::radians(p.outerAngle)),0,0}});
    SceneUniform scene{vp, glm::vec4(s.camera.position,1), glm::vec4(s.ambient,s.ambientIntensity),
        glm::vec4(safeDirection(s.direction),0), glm::vec4(s.directionalColor,s.directionalEnabled ? s.directionalIntensity : 0.f),
        {static_cast<int>(points.size()),static_cast<int>(spots.size()),0,0}};
    upload(sceneBuffer, &scene, sizeof(scene), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    upload(pointBuffer, points.data(), points.size()*sizeof(PointData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(spotBuffer, spots.data(), spots.size()*sizeof(SpotData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    bindBuffer(sceneSet,0,sceneBuffer,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    bindBuffer(sceneSet,1,pointBuffer,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    bindBuffer(sceneSet,2,spotBuffer,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    Material material = s.material; material.albedo *= s.object.tint;
    uploadModel(scene::model(s.object),material);
    std::vector<Vertex> cubes, lines;
    markers(s,cubes,lines);
    upload(cubeBuffer,cubes.data(),cubes.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    upload(lineBuffer,lines.data(),lines.size()*sizeof(Vertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    auto cmd = frame.command_buffer;
    graphics::check(vkResetCommandBuffer(cmd,0), "reset frame");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    graphics::check(vkBeginCommandBuffer(cmd,&begin), "begin frame");
    VkClearValue clear[2]{};
    clear[0].color = {{0.025f,0.035f,0.055f,1.f}}; clear[1].depthStencil = {1.f,0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = c.render_pass; pass.framebuffer = frame.framebuffer; pass.renderArea.extent = c.swapchain_extent;
    pass.clearValueCount = 2; pass.pClearValues = clear;
    vkCmdBeginRenderPass(cmd,&pass,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{float(left),0,float(width),float(c.swapchain_extent.height),0,1};
    VkRect2D scissor{{static_cast<int>(left),0},{width,c.swapchain_extent.height}};
    vkCmdSetViewport(cmd,0,1,&viewport); vkCmdSetScissor(cmd,0,1,&scissor);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,litPipeline);
    VkDescriptorSet sets[] = {sceneSet,modelSet};
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,lightingLayout,0,2,sets,0,nullptr);
    drawMesh(cmd,pyramidBuffer,pyramidCount);
    vkCmdPushConstants(cmd,markerLayout,VK_SHADER_STAGE_VERTEX_BIT,0,sizeof(vp),&vp);
    if (!cubes.empty()) {
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,cubePipeline);
        drawMesh(cmd,cubeBuffer.buffer,static_cast<uint32_t>(cubes.size()));
    }
    if (!lines.empty()) {
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,linePipeline);
        drawMesh(cmd,lineBuffer.buffer,static_cast<uint32_t>(lines.size()));
    }
    vkCmdEndRenderPass(cmd);
    graphics::check(vkEndCommandBuffer(cmd), "end frame");
}
}
