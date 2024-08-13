struct VertexInput 
{
    @location(0) position: vec3f,
    @location(1) color: vec3f,
};

struct VertexOutput 
{
    @builtin(position) position: vec4f,
    // The location here does not refer to a vertex attribute, it just means
    // that this field must be handled by the rasterizer.
    // (It can also refer to another field of another struct that would be used
    // as input to the fragment shader.)
    @location(0) color: vec3f,
};

/**
 * A structure holding the value of our uniforms
 */
 struct MyUniforms 
 {
    color: vec4f,
    time: f32,
 };

// The memory location of the uniform is given by a pair of a *bind group* and a *binding*
@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;

@vertex
fn vs_main(in: VertexInput) -> VertexOutput 
{
    let ratio = 1920.0 / 1080.0; // The width and height of the target surface
    var out: VertexOutput; // Create the output struct
    let angle = uMyUniforms.time; // you can multiply it go rotate faster
    let alpha = cos(angle);
    let beta = sin(angle);
    var position = vec3f(
        in.position.x,
        alpha * in.position.y + beta * in.position.z,
        alpha * in.position.z - beta * in.position.y,
    );

    out.position = vec4f(position.x , position.y * ratio, position.z * 0.5 + 0.5, 1.0);
    out.color = in.color; // Send input color over to frag shader
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f 
{
	// We multiply the scene's color with our global uniform (this is one
    // possible use of the color uniform, among many others).
    let color = in.color * uMyUniforms.color.rgb;
    return vec4f(color, uMyUniforms.color.a);
}