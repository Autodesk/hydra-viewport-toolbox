// Copyright 2025 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <hvt/api.h>

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <memory>
#include <vector>
#if defined(__cpp_concepts)
#include <concepts>
#else
#include <type_traits>
#include <utility>
#endif

namespace HVT_NS
{

// Forward declarations
class HdPageableBufferBase;
struct HdPagingContext;
struct HdPagingDecision;
struct HdSelectionContext;

namespace HdPagingConcepts {

#if defined(__cpp_concepts)

// Concept for objects that have an id (path)
template<typename T>
concept Pathed = requires(T t) {
    { t.Path() } -> std::convertible_to<PXR_NS::SdfPath>;
};

// Concept for objects that have a size
template<typename T>
concept Sized = requires(T t) {
    { t.Size() } -> std::convertible_to<std::size_t>;
};

// Concept for memory-managed objects
template<typename T>
concept MemoryManaged = requires(T t) {
    { t.PageToSceneMemory() } -> std::convertible_to<bool>;
    { t.PageToRendererMemory() } -> std::convertible_to<bool>;
    { t.PageToDisk() } -> std::convertible_to<bool>;
};

// Concept for aged resources
template<typename T>
concept Aged = requires(T t, int frame) {
    { t.FrameStamp() } -> std::convertible_to<int>;
    { t.UpdateFrameStamp(frame) } -> std::same_as<void>;
    { t.IsOverAge(frame, frame) } -> std::convertible_to<bool>;
};

// Combined concept for complete buffer-like objects
template<typename T>
concept BufferLike = Pathed<T> && Sized<T> && MemoryManaged<T> && Aged<T>;

// Concept for pageable buffer managers
template<typename T, typename BufferType>
concept BufferManagerLike = requires(T t, std::shared_ptr<BufferType> buffer, PXR_NS::SdfPath path, size_t size) {
    requires BufferLike<BufferType>;
    { t.CreateBuffer(path, size) } -> std::same_as<std::shared_ptr<BufferType>>;
    { t.RemoveBuffer(path) } -> std::same_as<void>;
    { t.FindBuffer(path) } -> std::convertible_to<std::shared_ptr<BufferType>>;
    { t.FreeCrawl() } -> std::same_as<void>;
};

// Concept for paging strategies
template<typename T>
concept PagingStrategyLike = requires(T t, const HdPageableBufferBase& buffer, const HdPagingContext& context) {
    { t(buffer, context) } -> std::convertible_to<HdPagingDecision>;
} || requires(T t, const HdPageableBufferBase& buffer, const HdPagingContext& context) {
    { t.operator()(buffer, context) } -> std::convertible_to<HdPagingDecision>;
};

// Concept for buffer selection strategies  
template<typename T, typename InputIterator>
concept BufferSelectionStrategyLike = requires(T t, InputIterator first, InputIterator last, const HdSelectionContext& context) {
    { t(first, last, context) } -> std::convertible_to<std::vector<std::shared_ptr<HdPageableBufferBase>>>;
} || requires(T t, InputIterator first, InputIterator last, const HdSelectionContext& context) {
    { t.operator()(first, last, context) } -> std::convertible_to<std::vector<std::shared_ptr<HdPageableBufferBase>>>;
};

#else  // !defined(__cpp_concepts)

// SFINAE-based detection helpers for pre-C++20 compilers

template<typename, typename = void>
struct Pathed : std::false_type {};
template<typename T>
struct Pathed<T, std::void_t<decltype(std::declval<T>().Path())>>
    : std::is_convertible<decltype(std::declval<T>().Path()), PXR_NS::SdfPath> {};


template<typename, typename = void>
struct Sized : std::false_type {};
template<typename T>
struct Sized<T, std::void_t<decltype(std::declval<T>().Size())>>
    : std::is_convertible<decltype(std::declval<T>().Size()), std::size_t> {};


template<typename, typename = void>
struct MemoryManaged : std::false_type {};
template<typename T>
struct MemoryManaged<T, std::void_t<
    decltype(std::declval<T>().PageToSceneMemory()),
    decltype(std::declval<T>().PageToRendererMemory()),
    decltype(std::declval<T>().PageToDisk())>>
{
    static constexpr bool value =
        std::is_convertible<decltype(std::declval<T>().PageToSceneMemory()), bool>::value &&
        std::is_convertible<decltype(std::declval<T>().PageToRendererMemory()), bool>::value &&
        std::is_convertible<decltype(std::declval<T>().PageToDisk()), bool>::value;
};

template<typename, typename = void>
struct Aged : std::false_type {};
template<typename T>
struct Aged<T, std::void_t<
    decltype(std::declval<T>().FrameStamp()),
    decltype(std::declval<T>().UpdateFrameStamp(0)),
    decltype(std::declval<T>().IsOverAge(0,0))>>
{
    static constexpr bool value =
        std::is_convertible<decltype(std::declval<T>().FrameStamp()), int>::value &&
        std::is_same<decltype(std::declval<T>().UpdateFrameStamp(0)), void>::value &&
        std::is_convertible<decltype(std::declval<T>().IsOverAge(0,0)), bool>::value;
};

// Traits that resemble pageable concepts

template<typename T>
struct BufferLike : std::integral_constant<bool,
    Pathed<T>::value &&
    Sized<T>::value &&
    MemoryManaged<T>::value &&
    Aged<T>::value> {};
template<typename T>
inline constexpr bool BufferLikeValue = BufferLike<T>::value;

template<typename, typename, typename = void>
struct _BufferManagerLike : std::false_type {};
template<typename T, typename BufferType>
struct _BufferManagerLike<T, BufferType, std::void_t<
    decltype(std::declval<T>().CreateBuffer(std::declval<PXR_NS::SdfPath>(), std::declval<size_t>())),
    decltype(std::declval<T>().RemoveBuffer(std::declval<PXR_NS::SdfPath>())),
    decltype(std::declval<T>().FindBuffer(std::declval<PXR_NS::SdfPath>())),
    decltype(std::declval<T>().FreeCrawl())>>
{
    static constexpr bool value =
        BufferLikeValue<BufferType> &&
        std::is_same<decltype(std::declval<T>().CreateBuffer(std::declval<PXR_NS::SdfPath>(), std::declval<size_t>())), std::shared_ptr<BufferType>>::value &&
        std::is_same<decltype(std::declval<T>().RemoveBuffer(std::declval<PXR_NS::SdfPath>())), void>::value &&
        std::is_convertible<decltype(std::declval<T>().FindBuffer(std::declval<PXR_NS::SdfPath>())), std::shared_ptr<BufferType>>::value &&
        std::is_same<decltype(std::declval<T>().FreeCrawl()), void>::value;
};

template<typename T, typename BufferType>
struct BufferManagerLike : std::integral_constant<bool, _BufferManagerLike<T, BufferType>::value> {};
template<typename T, typename BufferType>
inline constexpr bool BufferManagerLikeValue = BufferManagerLike<T, BufferType>::value;

template<typename, typename = void>
struct _PagingStrategyLike : std::false_type {};
template<typename T>
struct _PagingStrategyLike<T, std::void_t<decltype(std::declval<T>()(std::declval<const HdPageableBufferBase&>(), std::declval<const HdPagingContext&>()))>>
{
    static constexpr bool value = std::is_convertible<decltype(std::declval<T>()(std::declval<const HdPageableBufferBase&>(), std::declval<const HdPagingContext&>())), HdPagingDecision>::value;
};

template<typename T>
struct PagingStrategyLike : std::integral_constant<bool, _PagingStrategyLike<T>::value> {};
template<typename T>
inline constexpr bool PagingStrategyLikeValue = PagingStrategyLike<T>::value;

template<typename, typename = void, typename = void>
struct _BufferSelectionStrategyLike : std::false_type {};
template<typename T, typename InputIterator>
struct _BufferSelectionStrategyLike<T, InputIterator, std::void_t<decltype(std::declval<T>()(std::declval<InputIterator>(), std::declval<InputIterator>(), std::declval<const HdSelectionContext&>()))>>
{
    static constexpr bool value = std::is_convertible<decltype(std::declval<T>()(std::declval<InputIterator>(), std::declval<InputIterator>(), std::declval<const HdSelectionContext&>())), std::vector<std::shared_ptr<HdPageableBufferBase>>>::value;
};

template<typename T, typename InputIterator>
struct BufferSelectionStrategyLike : std::integral_constant<bool, _BufferSelectionStrategyLike<T, InputIterator>::value> {};
template<typename T, typename InputIterator>
inline constexpr bool BufferSelectionStrategyLikeValue = BufferSelectionStrategyLike<T, InputIterator>::value;

#endif // __cpp_concepts

} // namespace HdPagingConcepts 

} // namespace HVT_NS
