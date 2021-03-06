#pragma once

#include "geometry/point2d.hpp"

#include <glm_config.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include <glm/mat4x2.hpp>
#include <glm/mat4x3.hpp>

namespace glsl
{

using glm::vec2;
using glm::vec3;
using glm::vec4;

using glm::dvec2;
using glm::dvec3;
using glm::dvec4;

using glm::mat3;
using glm::mat4;
using glm::mat4x2;
using glm::mat4x3;

using glm::dmat3;
using glm::dmat4;
using glm::dmat4x2;
using glm::dmat4x3;

typedef vec4   Quad1;
typedef mat4x2 Quad2;
typedef mat4x3 Quad3;
typedef mat4   Quad4;

inline m2::PointF ToPoint(vec2 const & v)
{
  return m2::PointF(v.x, v.y);
}

inline vec2 ToVec2(m2::PointF const & pt)
{
  return glsl::vec2(pt.x, pt.y);
}

inline vec2 ToVec2(m2::PointD const & pt)
{
  return glsl::vec2(pt.x, pt.y);
}

template <typename T>
inline uint8_t GetComponentCount()
{
  ASSERT(false, ());
  return 0;
}

template <>
inline uint8_t GetComponentCount<vec2>()
{
  return 2;
}

template <>
inline uint8_t GetComponentCount<vec3>()
{
  return 3;
}

template <>
inline uint8_t GetComponentCount<vec4>()
{
  return 4;
}

} // namespace glsl
