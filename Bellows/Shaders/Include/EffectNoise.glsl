// Noise and edges shared by effect surfaces (Particle.frag) and their distortion (ParticleDistort.frag).

float Hash(vec2 cell)
{
	return fract(sin(dot(cell, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth value noise, 0 to 1, repeating every 'period' cells across X so a closed ring has no seam.
float ValueNoise(vec2 p, float period)
{
	vec2 cell = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	float x0 = mod(cell.x, period);
	float x1 = mod(cell.x + 1.0, period);
	float a = Hash(vec2(x0, cell.y));
	float b = Hash(vec2(x1, cell.y));
	float c = Hash(vec2(x0, cell.y + 1.0));
	float d = Hash(vec2(x1, cell.y + 1.0));
	return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// How solid an effect surface is across its V: soft toward both edges over 'edgeFade' of it.
float EdgeCoverage(float v, float edgeFade)
{
	return edgeFade > 0.0 ? smoothstep(0.0, edgeFade, min(v, 1.0 - v)) : 1.0;
}

// Dissolve noise, 0 to 1, from two octaves; 'cells' are the noise cells across U (whole, wrapping) and V.
float DissolveNoise(vec2 uv, vec2 cells)
{
	return ValueNoise(uv * cells, cells.x) * 0.65 + ValueNoise(uv * cells * 2.0 + 17.0, cells.x * 2.0) * 0.35;
}
