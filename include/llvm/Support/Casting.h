//===-- llvm/Support/Casting.h - Allow flexible, checked, casts -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the isa<X>(), cast<X>(), dyn_cast<X>(), cast_or_null<X>(),
// and dyn_cast_or_null<X>() templates.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CASTING_H
#define LLVM_SUPPORT_CASTING_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cassert>

namespace llvm {

//===----------------------------------------------------------------------===//
//                          isa<x> Support Templates
//===----------------------------------------------------------------------===//

// Define a template that can be specialized by smart pointers to reflect the
// fact that they are automatically dereferenced, and are not involved with the
// template selection process...  the default implementation is a noop.
//
/** 20140802 [study]
 * the default implementation
 */
/** 20140805 [eundoo.song]
 * general template for simplify_type
 */
template<typename From> struct simplify_type {
  typedef       From SimpleType;        // The real type this represents...

  // An accessor to get the real value...
  static SimpleType &getSimplifiedValue(From &Val) { return Val; }
};

/** 20140802 [study]
 * something like the following one???
 * Define a template that can be specialized by smart pointers to reflect the
 * fact that they are automatically dereferenced, and are not involved with the
 * template selection process
 * 추후 분석 필요...
 */
/** 20140805 [eundoo.song]
 * specialization template for simplify_type due to "const From"
 */
template<typename From> struct simplify_type<const From> {
	/** 20140805 [eundoo.song]
	 * 1. example  const int
	 *  1) typedef simplify_type<int>::SimpleType NonConstSimpleType;
	 *  1) typedef int NonConstSimpleType
	 *  2) typedef const int SimpleType
	 *  3) typedef const int &RetType
	 * 
	 * 2. example const int*
	 *  1) typedef simplify_type<int*>::SimpleType NonConstSimpleType;
	 *  1) typedef int *NonConstSimpleType
	 *  2) typedef const int *SimpleType
	 *  3) typedef const int *RetType
	 */
  typedef typename simplify_type<From>::SimpleType NonConstSimpleType; //1
  typedef typename add_const_past_pointer<NonConstSimpleType>::type //2
    SimpleType;
  typedef typename add_lvalue_reference_if_not_pointer<SimpleType>::type //3
    RetType;
  static RetType getSimplifiedValue(const From& Val) { //4
    return simplify_type<From>::getSimplifiedValue(const_cast<From&>(Val));
  }
};

// The core of the implementation of isa<X> is here; To and From should be
// the names of classes.  This template can be specialized to customize the
// implementation of isa<> without rewriting it from scratch.
/** 20140802 [study]
 * To에 정의된 classof 함수를 사용해서  isa관계를 판단한다.
 * LLVM 내부에 사용하는 class가 타입확인이 필요할 경우 각 정의된 class마다 classof를 정의해서 사용한다. 
 * 즉, C++에서 Standard로 제공하는 RTTI을 사용하는것이 아닌 LLVM 내부적으로 RTTI값을 정의해줘서 사용하도록 하고 있다.
 *  장점 : Standard RTTI보다 오버헤드가 없을듯. 성능 잇점. 
 *	  이유 : 간단하고 classof에서 사용하는 값이 const라 컴파일시에 계산될수 있을듯.
 *	단점 : LLVM에서 내부모듈을 추가할때마다 classof를 추가해워야 하는 불편한 점이 잇음.
 *
 *	참고1 : http://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html
 *	참고2 : http://llvm.org/docs/ProgrammersManual.html#isa
 *
 */
template <typename To, typename From, typename Enabler = void>
struct isa_impl {
  static inline bool doit(const From &Val) {
    return To::classof(&Val);
  }
};

/// \brief Always allow upcasts, and perform no dynamic check for them.
/** 20140802 [study]
 * about std::is_base_of<To, From>::value
 *   http://en.cppreference.com/w/cpp/types/is_base_of
 *   If Derived is derived from Base or if both are the same non-union class, provid
 *   es the member constant value equal to true. Otherwise value is false.
 *
 *   Requires that Derived is a complete type if it is not the same type as Base and * if both Base and Derived are class types.
 *  
 *
 *  std::enable_if<...>의 condition (첫번째 인자)가 true로 나올경우 이 함수를 실행한다. 
 *  즉, is_base_of로 To와 From의 관계에서 To가 부모이면 doit에서 true를 리턴한다.
 *  False일 경우,
 *		template <typename To, typename From, typename Enabler = void>
 *			struct isa_impl::doit(...)
 *			이 실행된다.
 *	이 모든것이 컴파일시 결정될수 있으므로 효율적일듯..???
 */
template <typename To, typename From>
struct isa_impl<
    To, From, typename std::enable_if<std::is_base_of<To, From>::value>::type> {
  static inline bool doit(const From &) { return true; }
};

/** 20140805 [eundoo.song]
 * general template for isa_impl_cl
 * 
 * the below templates having the same name are specialized depending on its From type.
 *
 */
template <typename To, typename From> struct isa_impl_cl {
  static inline bool doit(const From &Val) {
    return isa_impl<To, From>::doit(Val);
  }
};

template <typename To, typename From> struct isa_impl_cl<To, const From> {
  static inline bool doit(const From &Val) {
    return isa_impl<To, From>::doit(Val);
  }
};

template <typename To, typename From> struct isa_impl_cl<To, From*> {
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

template <typename To, typename From> struct isa_impl_cl<To, From*const> {
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

template <typename To, typename From> struct isa_impl_cl<To, const From*> {
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};

template <typename To, typename From> struct isa_impl_cl<To, const From*const> {
  static inline bool doit(const From *Val) {
    assert(Val && "isa<> used on a null pointer");
    return isa_impl<To, From>::doit(*Val);
  }
};


/** 20140805 [eundoo.song]
 *
 * general template for isa_impl_wrap
 */
template<typename To, typename From, typename SimpleFrom>
struct isa_impl_wrap {
  // When From != SimplifiedType, we can simplify the type some more by using
  // the simplify_type template.
  static bool doit(const From &Val) {
    return isa_impl_wrap<To, SimpleFrom,
      typename simplify_type<SimpleFrom>::SimpleType>::doit(
                          simplify_type<const From>::getSimplifiedValue(Val));
  }
};

/** 20140805 [eundoo.song]
 *
 *  specialization template for isa_impl_wrap (when both From types are same) 
 */
template<typename To, typename FromTy>
struct isa_impl_wrap<To, FromTy, FromTy> {
  // When From == SimpleType, we are as simple as we are going to get.
  static bool doit(const FromTy &Val) {
    return isa_impl_cl<To,FromTy>::doit(Val);
  }
};

// isa<X> - Return true if the parameter to the template is an instance of the
// template type argument.  Used like this:
//
//  if (isa<Type>(myVal)) { ... }
//
/** 20140805 [eundoo.song]
 * return isa_impl_wrap<X, const Y,  typename simplify_type<const Y>::SimpleType>::doit(Val);  
 *
 * return isa_impl_wrap<UndefValue, const Constant *,  typename simplify_type<const Constant*>::SimpleType>::doit(Val);
 * return isa_impl_wrap<UndefValue, const Constant *,  typename simplify_type<const Constant*>::SimpleType>::doit(Val);
 */
template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline bool isa(const Y &Val) {
  return isa_impl_wrap<X, const Y,
                       typename simplify_type<const Y>::SimpleType>::doit(Val);
}

//===----------------------------------------------------------------------===//
//                          cast<x> Support Templates
//===----------------------------------------------------------------------===//

template<class To, class From> struct cast_retty;


// Calculate what type the 'cast' function should return, based on a requested
// type of To and a source type of From.
template<class To, class From> struct cast_retty_impl {
  typedef To& ret_type;         // Normal case, return Ty&
};
template<class To, class From> struct cast_retty_impl<To, const From> {
  typedef const To &ret_type;   // Normal case, return Ty&
};

template<class To, class From> struct cast_retty_impl<To, From*> {
  typedef To* ret_type;         // Pointer arg case, return Ty*
};

template<class To, class From> struct cast_retty_impl<To, const From*> {
  typedef const To* ret_type;   // Constant pointer arg case, return const Ty*
};

template<class To, class From> struct cast_retty_impl<To, const From*const> {
  typedef const To* ret_type;   // Constant pointer arg case, return const Ty*
};


template<class To, class From, class SimpleFrom>
struct cast_retty_wrap {
  // When the simplified type and the from type are not the same, use the type
  // simplifier to reduce the type, then reuse cast_retty_impl to get the
  // resultant type.
  typedef typename cast_retty<To, SimpleFrom>::ret_type ret_type;
};

template<class To, class FromTy>
struct cast_retty_wrap<To, FromTy, FromTy> {
  // When the simplified type is equal to the from type, use it directly.
  typedef typename cast_retty_impl<To,FromTy>::ret_type ret_type;
};

template<class To, class From>
struct cast_retty {
  typedef typename cast_retty_wrap<To, From,
                   typename simplify_type<From>::SimpleType>::ret_type ret_type;
};

// Ensure the non-simple values are converted using the simplify_type template
// that may be specialized by smart pointers...
//
template<class To, class From, class SimpleFrom> struct cast_convert_val {
  // This is not a simple type, use the template to simplify it...
  static typename cast_retty<To, From>::ret_type doit(From &Val) {
    return cast_convert_val<To, SimpleFrom,
      typename simplify_type<SimpleFrom>::SimpleType>::doit(
                          simplify_type<From>::getSimplifiedValue(Val));
  }
};

template<class To, class FromTy> struct cast_convert_val<To,FromTy,FromTy> {
  // This _is_ a simple type, just cast it.
  static typename cast_retty<To, FromTy>::ret_type doit(const FromTy &Val) {
    typename cast_retty<To, FromTy>::ret_type Res2
     = (typename cast_retty<To, FromTy>::ret_type)const_cast<FromTy&>(Val);
    return Res2;
  }
};
/** 20140802 [study]
 * std::is_same -> X 와 SimpleType이 같은 Type인지 컴파일시 체크해서
 * value 값에 저장
 * 그런데 SimpleType은 무엇을 말하는걸까?
 *   1) template<typename From> struct simplify_type {
 *    typedef From SimpleType;
 *
 *   }
 *
 *   2) template<typename From> struct simplify_type<const From> {  
 *     ...
 *   }
 *   1) 2) 으로 불려질수 있다.
 *   ??? 궁금한점
 *   - simplify_type 주석을 보면 smart pointer의 automatic dereferenced와 관련이 
 *   되어 있는것 처럼 설명이 되어 있는데 2)과 관련된 구현으로 생각됨
 *   코드 분석은 나중에...
 *   - 이경우가 아니라면 1)처럼 default impl. no-operation으로 작동한다.
 */ 
template <class X> struct is_simple_type {
  static const bool value =
      std::is_same<X, typename simplify_type<X>::SimpleType>::value;
};

// cast<X> - Return the argument parameter cast to the specified type.  This
// casting operator asserts that the type is correct, so it does not return null
// on failure.  It does not allow a null argument (use cast_or_null for that).
// It is typically used like this:
//
//  cast<Instruction>(myVal)->getParent()
//
/** 20140802 [study]
 * 다음 시간
 */
template <class X, class Y>
inline typename std::enable_if<!is_simple_type<Y>::value,
                               typename cast_retty<X, const Y>::ret_type>::type
cast(const Y &Val) {
  assert(isa<X>(Val) && "cast<Ty>() argument of incompatible type!");
  return cast_convert_val<
      X, const Y, typename simplify_type<const Y>::SimpleType>::doit(Val);
}

template <class X, class Y>
inline typename cast_retty<X, Y>::ret_type cast(Y &Val) {
  assert(isa<X>(Val) && "cast<Ty>() argument of incompatible type!");
  return cast_convert_val<X, Y,
                          typename simplify_type<Y>::SimpleType>::doit(Val);
}

template <class X, class Y>
inline typename cast_retty<X, Y *>::ret_type cast(Y *Val) {
  assert(isa<X>(Val) && "cast<Ty>() argument of incompatible type!");
  return cast_convert_val<X, Y*,
                          typename simplify_type<Y*>::SimpleType>::doit(Val);
}

// cast_or_null<X> - Functionally identical to cast, except that a null value is
// accepted.
//
template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline typename cast_retty<X, Y *>::ret_type
cast_or_null(Y *Val) {
  if (!Val) return nullptr;
  assert(isa<X>(Val) && "cast_or_null<Ty>() argument of incompatible type!");
  return cast<X>(Val);
}


// dyn_cast<X> - Return the argument parameter cast to the specified type.  This
// casting operator returns null if the argument is of the wrong type, so it can
// be used to test for a type as well as cast if successful.  This should be
// used in the context of an if statement like this:
//
//  if (const Instruction *I = dyn_cast<Instruction>(myVal)) { ... }
//
/** 20140802 [study]
 * isa : Val 가 X와 상속관계에 있는지 검사  
 * 상속관계에 있으면 X로 cast 한다. 
 * std::enable_if 의 첫번째 인자의 결과가 True이면 두번째 인자 type이 설정이 되고 아니면
 * definition 할 type없으므로 clang에서는 컴파일에러가 난다.
 */
template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline typename 
std::enable_if<!is_simple_type<Y>::value, typename cast_retty<X, const Y>::ret_type>::type
dyn_cast(const Y &Val) {
  return isa<X>(Val) ? cast<X>(Val) : nullptr;
}

template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline typename cast_retty<X, Y>::ret_type
dyn_cast(Y &Val) {
  return isa<X>(Val) ? cast<X>(Val) : nullptr;
}

template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline typename cast_retty<X, Y *>::ret_type
dyn_cast(Y *Val) {
  return isa<X>(Val) ? cast<X>(Val) : nullptr;
}

// dyn_cast_or_null<X> - Functionally identical to dyn_cast, except that a null
// value is accepted.
//
template <class X, class Y>
LLVM_ATTRIBUTE_UNUSED_RESULT inline typename cast_retty<X, Y *>::ret_type
dyn_cast_or_null(Y *Val) {
  return (Val && isa<X>(Val)) ? cast<X>(Val) : nullptr;
}

} // End llvm namespace

#endif
