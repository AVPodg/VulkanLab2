#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 color;
layout(push_constant) uniform Push { mat4 viewProjection; } push;
layout(location=0) out vec3 markerColor;
void main() {
    markerColor=color;
    gl_Position=push.viewProjection*vec4(position,1);
}
