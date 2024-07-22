#pragma once

#ifdef _MSC_VER
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif

namespace detail {
#ifndef _MSC_VER
template<typename T>
static __forceinline float32x4_t reinterpret_to_f32(T v);
template<typename T>
static __forceinline T reinterpret_from_f32(float32x4_t v);

template<> __forceinline float32x4_t reinterpret_to_f32<float32x4_t>(float32x4_t v) { return v; }
template<> __forceinline float32x4_t reinterpret_to_f32<int32x4_t>(int32x4_t v) { return vreinterpretq_f32_s32(v); }
template<> __forceinline float32x4_t reinterpret_to_f32<uint32x4_t>(uint32x4_t v) { return vreinterpretq_f32_u32(v); }

template<> __forceinline float32x4_t reinterpret_from_f32<float32x4_t>(float32x4_t v) { return v; }
template<> __forceinline int32x4_t reinterpret_from_f32<int32x4_t>(float32x4_t v) { return vreinterpretq_s32_f32(v); }
template<> __forceinline uint32x4_t reinterpret_from_f32<uint32x4_t>(float32x4_t v) { return vreinterpretq_u32_f32(v); }
#else
template <typename T>
static __forceinline float32x4_t reinterpret_to_f32(T v) { return v; }
template <typename T>
static __forceinline T reinterpret_from_f32(float32x4_t v) { return v; }
#endif
}

template<int i0, int i1, int i2, int i3, typename T>
static __forceinline T _neon_permute(T value)
{
  if constexpr (i0 == 0 && i1 == 1 && i2 == 2 && i3 == 3)
    return value;

	const float32x4_t fvalue = detail::reinterpret_to_f32(value);

	float32x4_t ret;
	if constexpr (i0 == i1 && i1 == i2 && i2 == i3)
	{
		ret = vdupq_laneq_f32(fvalue, i0);
	}
	else if constexpr (i0 == i2 && i1 == i3)
	{
		ret = vdupq_laneq_f32(fvalue, i0);
		ret = vcopyq_laneq_f32(ret, 1, fvalue, i1);
		// ret = (float32x4_t)(vcopyq_lane_f64((float64x2_t)ret, 1, (float64x2_t)ret, 0));
		ret = vreinterpretq_f32_f64(vcombine_f64(vget_low_f64(vreinterpretq_f64_f32(ret)), vget_low_f64(vreinterpretq_f64_f32(ret))));
	}
	else
	{
		ret = vdupq_laneq_f32(fvalue, i0);
		ret = vcopyq_laneq_f32(ret, 1, fvalue, i1);
		ret = vcopyq_laneq_f32(ret, 2, fvalue, i2);
		ret = vcopyq_laneq_f32(ret, 3, fvalue, i3);
	}

	return detail::reinterpret_from_f32<T>(ret);
}

template<int i0, int i1, int i2, int i3, typename T>
static __forceinline T _neon_permute_lohi(T lovalue, T hivalue)
{
  const float32x4_t lofvalue = detail::reinterpret_to_f32(lovalue);
  const float32x4_t hifvalue = detail::reinterpret_to_f32(hivalue);

	float32x4_t ret;
	if constexpr (i0 == 0 && i1 == 1 && i2 == 2 && i3 == 3)
	{
		ret = vreinterpretq_f32_f64(vcombine_f64(vget_low_f64(vreinterpretq_f64_f32(lofvalue)), vget_high_f64(vreinterpretq_f64_f32(hifvalue))));
	}
	else if constexpr (i0 == 0 && i1 == 1 && i2 == 0 && i3 == 1)
	{
		ret = vreinterpretq_f32_f64(vcombine_f64(vget_low_f64(vreinterpretq_f64_f32(lofvalue)), vget_low_f64(vreinterpretq_f64_f32(hifvalue))));
	}
#if 0
	else if constexpr (i0 == i1 && i2 == i3)
	{
		ret = vcombine_f64(vget_low_f64(vdupq_laneq_f32(lofvalue, i0)), vget_low_f64(vdupq_laneq_f32(hifvalue, i2)));
	}
	else if constexpr (i0 == i2 && i1 == i3)
	{
		ret = vdupq_laneq_f32(fvalue, i0);
		ret = vcopyq_laneq_f32(ret, 1, fvalue, i1);
		// ret = (float32x4_t)(vcopyq_lane_f64((float64x2_t)ret, 1, (float64x2_t)ret, 0));
		ret = vreinterpretq_f32_f64(vcombine_f64(vget_low_f64(vreinterpretq_f64_f32(ret)), vget_low_f64(vreinterpretq_f64_f32(ret))));
	}
#endif
	else
	{
		ret = vdupq_laneq_f32(lofvalue, i0);
		ret = vcopyq_laneq_f32(ret, 1, lofvalue, i1);
		ret = vcopyq_laneq_f32(ret, 2, hifvalue, i2);
		ret = vcopyq_laneq_f32(ret, 3, hifvalue, i3);
	}

	return detail::reinterpret_from_f32<T>(ret);
}

template<int mask, typename T>
static __forceinline T _neon_blend(T a, T b)
{
  if constexpr (mask == 0)
    return a;
  else if constexpr (mask == 0xf)
    return b;

  const float32x4_t afvalue = detail::reinterpret_to_f32(a);
  const float32x4_t bfvalue = detail::reinterpret_to_f32(b);

  float32x4_t ret = afvalue;

  if constexpr ((mask & 3) == 3)
  {
    ret = vreinterpretq_f32_f64(vcopyq_laneq_f64(vreinterpretq_f64_f32(ret), 0, vreinterpretq_f64_f32(bfvalue), 0));
  }
  else
  {
    if constexpr (mask & 1)
      ret = vcopyq_laneq_f32(ret, 0, bfvalue, 0);
    if constexpr (mask & 2)
      ret = vcopyq_laneq_f32(ret, 1, bfvalue, 1);
  }

  if constexpr ((mask & 12) == 12)
  {
    ret = vreinterpretq_f32_f64(vcopyq_laneq_f64(vreinterpretq_f64_f32(ret), 1, vreinterpretq_f64_f32(bfvalue), 1));
  }
  else
  {
    if constexpr (mask & 1)
      ret = vcopyq_laneq_f32(ret, 2, bfvalue, 2);
    if constexpr (mask & 2)
      ret = vcopyq_laneq_f32(ret, 3, bfvalue, 3);
  }

  return detail::reinterpret_from_f32<T>(ret);
}