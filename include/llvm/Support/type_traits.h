//===- llvm/Support/type_traits.h - Simplfied type traits -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides useful additions to the standard type_traits library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TYPE_TRAITS_H
#define LLVM_SUPPORT_TYPE_TRAITS_H

#include <type_traits>
#include <utility>

#ifndef __has_feature
#define LLVM_DEFINED_HAS_FEATURE
#define __has_feature(x) 0
#endif

namespace llvm {

/// isPodLike - This is a type trait that is used to determine whether a given
/// type can be copied around with memcpy instead of running ctors etc.
template <typename T>
struct isPodLike {
#if __has_feature(is_trivially_copyable)
  // If the compiler supports the is_trivially_copyable trait use it, as it
  // matches the definition of isPodLike closely.
  static const bool value = __is_trivially_copyable(T);
#else
  // If we don't know anything else, we can (at least) assume that all non-class
  // types are PODs.
  static const bool value = !std::is_class<T>::value;
#endif
};

// std::pair's are pod-like if their elements are.
template<typename T, typename U>
struct isPodLike<std::pair<T, U> > {
  static const bool value = isPodLike<T>::value && isPodLike<U>::value;
};

/// \brief Metafunction that determines whether the given type is either an
/// integral type or an enumeration type.
///
/// Note that this accepts potentially more integral types than is_integral
/// because it is based on merely being convertible implicitly to an integral
/// type.
template <typename T> class is_integral_or_enum {
  typedef typename std::remove_reference<T>::type UnderlyingT;

public:
  static const bool value =
      !std::is_class<UnderlyingT>::value && // Filter conversion operators.
      !std::is_pointer<UnderlyingT>::value &&
      !std::is_floating_point<UnderlyingT>::value &&
      std::is_convertible<UnderlyingT, unsigned long long>::value;
};

/// \brief If T is a pointer, just return it. If it is not, return T&.
template<typename T, typename Enable = void>
struct add_lvalue_reference_if_not_pointer { typedef T &type; };

template <typename T>
struct add_lvalue_reference_if_not_pointer<
    T, typename std::enable_if<std::is_pointer<T>::value>::type> {
  typedef T type;
};

/// \brief If T is a pointer to X, return a pointer to const X. If it is not,
/// return const T.
/** 20140805 [eundoo.song]
 * general template for add_const_past_pointer
 */
template<typename T, typename Enable = void>
struct add_const_past_pointer { typedef const T type; };

template <typename T>
struct add_const_past_pointer<
    T, typename std::enable_if<std::is_pointer<T>::value>::type> {
/** 20140805 [eundoo.song]
 * remove_pointer 한후 *는 다시 붙이는데 이유는???
 *
 */
/** 20140809 [study]
 * 포인터가 가르키는 내용에 const를 하려고 한다.
 * example
 *   if T is int*
 *     typedef (const int) * type;
 */
  typedef const typename std::remove_pointer<T>::type *type;
};

}

#ifdef LLVM_DEFINED_HAS_FEATURE
#undef __has_feature
#endif

#endif
