//
// Copyright (c) 2016-2020 Kris Jusiak (kris at jusiak dot net)
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_SML_UTILITY_SM_POOL_HPP
#define BOOST_SML_UTILITY_SM_POOL_HPP

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "boost/sml.hpp"

BOOST_SML_NAMESPACE_BEGIN

namespace utility {

template <class TEvent>
struct indexed_event {
  std::size_t id{};
  TEvent event{};
};

template <class TEvent>
constexpr indexed_event<TEvent> with_id(const std::size_t id, const TEvent& event = {}) {
  return indexed_event<TEvent>{id, event};
}

template <class TStorage, class TSm, class... TPolicies>
class sm_pool {
 public:
  using storage_type = TStorage;
  using sm_type = sm<TSm, TPolicies...>;

  sm_pool() : storage_(), sm_(storage_) {}

  explicit sm_pool(const std::size_t size)
      : storage_(make_storage(size, std::is_constructible<storage_type, std::size_t>{})), sm_(storage_) {}

  storage_type& storage() noexcept { return storage_; }
  const storage_type& storage() const noexcept { return storage_; }

  void reset() { storage_.reset(); }

  template <class TEvent>
  bool process_indexed(const std::size_t id, const TEvent& event = {}) {
    return sm_.process_event(indexed_event<TEvent>{id, event});
  }

  template <class TEvent, class TIt>
  std::size_t process_indexed_batch(TIt first, TIt last, const TEvent& event = {}) {
    std::size_t handled = 0;
    indexed_event<TEvent> indexed{0u, event};
    for (; first != last; ++first) {
      indexed.id = static_cast<std::size_t>(*first);
      handled += static_cast<std::size_t>(sm_.process_event(indexed));
    }
    return handled;
  }

  template <class TEvent, class TRange>
  std::size_t process_indexed_batch(const TRange& ids, const TEvent& event = {}) {
    return process_indexed_batch<TEvent>(std::begin(ids), std::end(ids), event);
  }

  template <class TIndexedEvent>
  bool process_event(const TIndexedEvent& event) {
    return sm_.process_event(event);
  }

  template <class TIt>
  std::size_t process_event_batch(TIt first, TIt last) {
    std::size_t handled = 0;
    for (; first != last; ++first) {
      handled += static_cast<std::size_t>(sm_.process_event(*first));
    }
    return handled;
  }

  template <class TRange>
  std::size_t process_event_batch(const TRange& events) {
    return process_event_batch(std::begin(events), std::end(events));
  }

 private:
  static storage_type make_storage(const std::size_t size, const std::true_type) { return storage_type(size); }
  static storage_type make_storage(const std::size_t, const std::false_type) { return storage_type(); }

  storage_type storage_;
  sm_type sm_;
};

}  // namespace utility

BOOST_SML_NAMESPACE_END

#endif  // BOOST_SML_UTILITY_SM_POOL_HPP
