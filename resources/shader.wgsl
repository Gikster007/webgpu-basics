struct VertexInput 
{
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
	@location(2) color: vec3f
};

struct VertexOutput 
{
    @builtin(position) position: vec4f,
    // The location here does not refer to a vertex attribute, it just means
    // that this field must be handled by the rasterizer.
    // (It can also refer to another field of another struct that would be used
    // as input to the fragment shader.)
    @location(0) color: vec3f,
	@location(1) normal: vec3f,
};

/**
 * A structure holding the value of our uniforms
 */
 struct MyUniforms 
 {
	proj: mat4x4f,
    view: mat4x4f,
    model: mat4x4f,
    color: vec4f,
    time: f32,
 };

// The memory location of the uniform is given by a pair of a *bind group* and a *binding*
@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;
@group(0) @binding(1) var gradientTexture: texture_2d<f32>;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput 
{
    var out: VertexOutput;
    out.position = uMyUniforms.proj * uMyUniforms.view * uMyUniforms.model * vec4f(in.position, 1.0);
    out.color = in.color;
	out.normal = (uMyUniforms.model * vec4f(in.normal, 0.0)).xyz;;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f 
{
	// let normal = normalize(in.normal);

	// // We multiply the scene's color with our global uniform (this is one
    // // possible use of the color uniform, among many others).
    // //let color = in.color * uMyUniforms.color.rgb;
    // let lightDirection1 = vec3f(0.5, -0.9, 0.1);
    // let lightDirection2 = vec3f(0.2, 0.4, 0.3);
	// let lightColor1 = vec3f(1.0, 0.9, 0.6);
    // let lightColor2 = vec3f(0.6, 0.9, 1.0);
    // let shading1 = max(0.0, dot(lightDirection1, normal));
    // let shading2 = max(0.0, dot(lightDirection2, normal));
    // let shading = shading1 * lightColor1 + shading2 * lightColor2;
    // let color = in.color * shading;

    // return vec4f(color, uMyUniforms.color.a);

    let color = textureLoad(gradientTexture, vec2i(in.position.xy), 0).rgb;
    return vec4f(color, 1.0);
}