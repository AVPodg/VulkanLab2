#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec3 vertexColor;
layout(location=0) out vec4 outColor;
layout(set=0,binding=0,std140) uniform Scene {
    mat4 viewProjection;
    vec4 camera;
    vec4 ambient;
    vec4 direction;
    vec4 directionalColor;
    ivec4 counts;
} scene;
struct Point { vec4 position; vec4 color; };
struct Spot { vec4 position; vec4 direction; vec4 color; vec4 angles; };
// Runtime arrays: no fixed maximum number of lights in the shader.
layout(set=0,binding=1,std430) readonly buffer Points { Point values[]; } points;
layout(set=0,binding=2,std430) readonly buffer Spots { Spot values[]; } spots;
layout(set=1,binding=0,std140) uniform Model {
    mat4 transform;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
    vec4 parameters;
} model;
vec3 unitVector(vec3 v) { return v * inversesqrt(max(dot(v,v), 0.000001)); }
vec3 blinnPhong(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo) {
    float diffuse = max(dot(N,L),0);
    vec3 H = unitVector(L+V);
    float highlight = diffuse > 0 ? pow(max(dot(N,H),0),model.parameters.x) : 0;
    return radiance * (albedo * diffuse + model.specular.rgb * highlight);
}
void main() {
    vec3 N = unitVector(worldNormal);
    vec3 V = unitVector(scene.camera.xyz-worldPosition);
    vec3 albedo = model.albedo.rgb;
    if (model.parameters.y > 0.5) albedo *= vertexColor;
    vec3 result = scene.ambient.rgb * scene.ambient.a * albedo;
    result += blinnPhong(-scene.direction.xyz, scene.directionalColor.rgb * scene.directionalColor.a,N,V,albedo);
    for (int i=0; i<scene.counts.x; ++i) {
        vec3 delta = points.values[i].position.xyz-worldPosition;
        float distanceSquared = max(dot(delta,delta),0.0001);
        vec3 radiance = points.values[i].color.rgb * points.values[i].color.a / distanceSquared;
        result += blinnPhong(unitVector(delta),radiance,N,V,albedo);
    }
    for (int i=0; i<scene.counts.y; ++i) {
        Spot light = spots.values[i];
        vec3 delta = light.position.xyz-worldPosition;
        float distanceSquared = max(dot(delta,delta),0.0001);
        vec3 L = unitVector(delta);
        float cosine = dot(-L,light.direction.xyz);
        float inner = light.angles.x, outer = light.angles.y;
        // Equal angles give a hard boundary, otherwise a smooth transition.
        float cone = inner > outer + 0.000001 ? smoothstep(outer,inner,cosine) : step(outer,cosine);
        vec3 radiance = light.color.rgb * light.color.a * cone / distanceSquared;
        result += blinnPhong(L,radiance,N,V,albedo);
    }
    outColor = vec4(result,1);
}
