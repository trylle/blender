
#pragma BLENDER_REQUIRE(common_view_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_lib.glsl)
#pragma BLENDER_REQUIRE(common_math_geom_lib.glsl)
#pragma BLENDER_REQUIRE(eevee_closure_lib.glsl)

/* -------------------------------------------------------------------- */
/** \name Encoding and decoding functions
 *
 * \{ */

uint gbuffer_encode_unit_float_to_uint(float scalar, const uint bit_size)
{
  float fac = float((1u << bit_size) - 1u);
  return uint(saturate(scalar) * fac);
}

float gbuffer_decode_unit_float_from_uint(uint packed_scalar, const uint bit_size)
{
  float fac = 1.0 / float((1u << bit_size) - 1u);
  uint mask = ~(0xFFFFFFFFu << bit_size);
  return float(packed_scalar & mask) * fac;
}

/* Expects input to be normalized. */
vec2 gbuffer_encode_normal(vec3 normal)
{
  return normal_encode(normal_world_to_view(normal));
}

vec3 gbuffer_decode_normal(vec2 packed_normal)
{
  return normal_view_to_world(normal_decode(packed_normal));
}

/* Note: does not handle negative colors. */
uint gbuffer_encode_color(vec3 color)
{
  color *= 1.0; /* Test */
  float intensity = length(color);
  /* Normalize to store it like a normal vector. */
  // color *= safe_rcp(intensity);

  uint encoded_color;
  // encoded_color = gbuffer_encode_unit_float_to_uint(saturate(color.x), 10u) << 10u;
  // encoded_color |= gbuffer_encode_unit_float_to_uint(saturate(color.y), 10u);
  // encoded_color |= gbuffer_encode_unit_float_to_uint(saturate(intensity), 12u) << 20u;

  encoded_color = gbuffer_encode_unit_float_to_uint(saturate(color.x), 11u);
  encoded_color |= gbuffer_encode_unit_float_to_uint(saturate(color.y), 11u) << 11u;
  encoded_color |= gbuffer_encode_unit_float_to_uint(saturate(color.z), 10u) << 21u;
  return encoded_color;
}

vec3 gbuffer_decode_color(uint packed_data)
{
  vec3 color;
  // color.x = gbuffer_decode_unit_float_from_uint(packed_data >> 10u, 10u);
  // color.y = gbuffer_decode_unit_float_from_uint(packed_data, 10u);
  // color.z = sqrt(1.0 - clamp(dot(color.xy, color.xy), 0.0, 1.0));
  // color *= gbuffer_decode_unit_float_from_uint(packed_data >> 20u, 12u);

  color.x = gbuffer_decode_unit_float_from_uint(packed_data, 11u);
  color.y = gbuffer_decode_unit_float_from_uint(packed_data >> 11u, 11u);
  color.z = gbuffer_decode_unit_float_from_uint(packed_data >> 21u, 10u);
  color *= 1.0; /* Test */
  return color;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Diffuse data
 *
 * \{ */

ClosureDiffuse gbuffer_load_diffuse_data(vec4 color_in, vec4 normal_in, vec4 data_in)
{
  ClosureDiffuse data_out;
  if (normal_in.x > 0.0) {
    data_out.color = color_in.rgb;
    data_out.N = gbuffer_decode_normal(normal_in.xy);
  }
  else {
    data_out.color = vec3(0.0);
    data_out.N = vec3(1.0);
  }
  // data_out.thickness = normal_in.w;
  data_out.sss_radius = data_in.rgb;
  data_out.sss_id = uint(normal_in.z * 1024.0);
  return data_out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Glossy data
 *
 * \{ */

ClosureReflection gbuffer_load_reflection_data(vec4 color_in, vec4 normal_in)
{
  ClosureReflection data_out;
  data_out.color = color_in.rgb;
  data_out.N = gbuffer_decode_normal(normal_in.xy);
  // data_out.thickness = normal_in.w;
  data_out.roughness = normal_in.z;
  return data_out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Refraction data
 *
 * \{ */

ClosureRefraction gbuffer_load_refraction_data(vec4 color_in, vec4 normal_in, vec4 data_in)
{
  ClosureRefraction data_out;
  if (normal_in.x < 0.0) {
    data_out.color = color_in.rgb;
    data_out.N = gbuffer_decode_normal(-normal_in.xy);
  }
  else {
    data_out.color = vec3(0.0);
    data_out.N = vec3(1.0);
  }
  data_out.ior = data_in.x;
  data_out.roughness = data_in.y;
  return data_out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Volume data
 *
 * Pack all volumetric effects.
 *
 * \{ */

#define VOLUME_HETEROGENEOUS -2.0

uvec4 gbuffer_store_volume_data(ClosureVolume data_in)
{
  uvec4 data_out;
  data_out.x = gbuffer_encode_color(data_in.emission);
  data_out.y = gbuffer_encode_color(data_in.scattering);
  data_out.z = gbuffer_encode_color(data_in.transmittance);
  data_out.w = floatBitsToUint(data_in.anisotropy);
  return data_out;
}

ClosureVolume gbuffer_load_volume_data(usampler2D gbuffer_tx, vec2 uv)
{
  uvec4 data_in = texture(gbuffer_tx, uv);

  ClosureVolume data_out;
  data_out.emission = gbuffer_decode_color(data_in.x);
  data_out.scattering = gbuffer_decode_color(data_in.y);
  data_out.transmittance = gbuffer_decode_color(data_in.z);
  data_out.anisotropy = uintBitsToFloat(data_in.w);
  return data_out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Emission data
 *
 * \{ */

vec3 gbuffer_store_emission_data(ClosureEmission data_in)
{
  return data_in.emission;
}

ClosureEmission gbuffer_load_emission_data(sampler2D gbuffer_tx, vec2 uv)
{
  vec4 data_in = texture(gbuffer_tx, uv);

  ClosureEmission data_out;
  data_out.emission = data_in.xyz;
  return data_out;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Transparency data
 *
 * \{ */

vec4 gbuffer_store_transparency_data(ClosureTransparency data_in)
{
  vec4 data_out;
  data_out.xyz = data_in.transmittance;
  data_out.w = data_in.holdout;
  return data_out;
}

ClosureTransparency gbuffer_load_transparency_data(sampler2D gbuffer_tx, vec2 uv)
{
  vec4 data_in = texture(gbuffer_tx, uv);

  ClosureTransparency data_out;
  data_out.transmittance = data_in.xyz;
  data_out.holdout = data_in.w;
  return data_out;
}

/** \} */