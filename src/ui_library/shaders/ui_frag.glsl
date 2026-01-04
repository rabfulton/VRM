#version 330 core
//out vec4 Out_Color;
// gl_FragColor is reserved keyword for the output
// or it  can be specified as type out
// -----------------------------------------
// gl_Frag_coord is reserved keyword for the xy texture coords
// it is a varying type as it is different for each fragment/pixel
// vec2 st = gl_FragCoord.xy;
// 0.5, 0.5 is lower left upto screen res - 0.5, dealing with pixel centers here
// -----------------------------------------
// can use ifdef endif like c macro
// Automatic casting can not be relied upon, always write 1.0 not 1
// -----------------------------------------
// step(threshold, varying);
// > threshold = 1.0, < threshold = 0.0
// -----------------------------------------
// smoothstep(start, finish, val_to_interpolate); Y = -3x^3 + 2x^2                   _
// if input < start, val = 0; if input > end, val = 1; if in_range _/
// -----------------------------------------
// fract(val or vector); returns x - floor(x), note for neg val this subtracts -1 for range 0 <-> -1
// floor(val); nearest integer below, ceil(val); nearest integer above.
// -----------------------------------------
// Plotting Function: 0.02 is line width, st = pixel coordinates, 
// float plot(vec2 st, float pct){
//   return  smoothstep( pct - 0.02, pct, st.y) -
//           smoothstep( pct, pct + 0.02, st.y);
// }
// -----------------------------------------
// mix(vec, vec, 0->1); interpolates between to vectors
// -----------------------------------------
out vec4 Out_Color;
in vec4 Frag_Color;
in vec4 position;
in vec2 tcoord;
uniform sampler2D texture_id;

void main(){

	vec4 sampled = vec4(1.0, 1.0, 1.0, texture(texture_id, tcoord).r);
	Out_Color = Frag_Color * sampled;

	//gl_FragColor = out_col * sampled;

}

