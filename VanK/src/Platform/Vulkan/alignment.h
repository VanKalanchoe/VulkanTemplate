/*-------------------------------------------------------------------------------------------------
Contains functions for aligning numbers to power-of-two boundaries.
-------------------------------------------------------------------------------------------------*/

#pragma once
#include <cstddef>  // for size_t

namespace VanK 
{
  /*-------------------------------------------------------------------------------------------------
  # Function `is_aligned<integral>(x, a)`
  Returns whether `x` is a multiple of `a`. `a` must be a power of two.
  -------------------------------------------------------------------------------------------------*/
  template <class integral>
  constexpr bool is_aligned(integral x, size_t a) noexcept
  {
    return (x & (integral(a) - 1)) == 0;
  }

  /*-------------------------------------------------------------------------------------------------
  # Function `align_up<integral>(x, a)`
  Rounds `x` up to a multiple of `a`. `a` must be a power of two.
  -------------------------------------------------------------------------------------------------*/
  template <class integral>
  constexpr integral align_up(integral x, size_t a) noexcept
  {
    return integral((x + (integral(a) - 1)) & ~integral(a - 1));
  }

  /*-------------------------------------------------------------------------------------------------
  # Function `align_down<integral>(x, a)`
  Rounds `x` down to a multiple of `a`. `a` must be a power of two.
  -------------------------------------------------------------------------------------------------*/
  template <class integral>
  constexpr integral align_down(integral x, size_t a) noexcept
  {
    return integral(x & ~integral(a - 1));
  }
}  // namespace nvutils

