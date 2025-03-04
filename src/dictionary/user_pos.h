// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef MOZC_DICTIONARY_USER_POS_H_
#define MOZC_DICTIONARY_USER_POS_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/container/serialized_string_array.h"
#include "data_manager/data_manager.h"

namespace mozc {
namespace dictionary {

// This implementation of UserPos uses a sorted array of tokens to
// efficiently lookup required data.  There are two required data, string array
// and token array, which are generated by ./gen_user_pos_data.py.
//
// * Prerequisite
// Little endian is assumed.
//
// * Binary format
//
// ** String array
// All the strings, such as key and value suffixes, are serialized into one
// array using SerializedStringArray in such a way that array is sorted in
// ascending order.  In the token array (see below), every string is stored as
// an index to this array.
//
// ** Token array
//
// The token array is an array of 8 byte blocks each of which has the following
// layout:
//
// Token layout (8 bytes)
// +---------------------------------------+
// | POS index  (2 bytes)                  |
// + - - - - - - - - - - - - - - - - - - - +
// | Value suffix index (2 bytes)          |
// + - - - - - - - - - - - - - - - - - - - +
// | Key suffix index  (2 bytes)           |
// + - - - - - - - - - - - - - - - - - - - +
// | Conjugation ID (2 bytes)              |
// +---------------------------------------+
//
// The array is sorted in ascending order of POS index so that we can use binary
// search to lookup necessary information efficiently.  Note that there are
// tokens having the same POS index.
class UserPos {
 public:
  static constexpr size_t kTokenByteLength = 8;

  struct Token {
    std::string key;
    std::string value;
    uint16_t id = 0;
    uint16_t attributes = 0;
    // The actual cost of user dictionary entries are populated
    // in the dictionary lookup time via PopulateTokenFromUserPosToken.
    std::string comment;  // This field comes from user dictionary.

    // Attribute is used to dynamically assign cost, and is independent from the
    // POS.
    enum Attribute {
      SHORTCUT = 1,  // Added via android shortcut, which has no explicit POS.
      ISOLATED_WORD = 2,    // 短縮よみ
      SUGGESTION_ONLY = 4,  //  SUGGESTION only.
      NON_JA_LOCALE = 8     // Locale is not Japanese.
    };

    inline void add_attribute(Attribute attr) { attributes |= attr; }
    inline bool has_attribute(Attribute attr) const {
      return attributes & attr;
    }
    inline void remove_attribute(Attribute attr) { attributes &= ~attr; }

    void swap(Token &other) noexcept {
      static_assert(std::is_nothrow_swappable_v<std::string>);
      using std::swap;
      swap(key, other.key);
      swap(value, other.value);
      swap(id, other.id);
      swap(attributes, other.attributes);
      swap(comment, other.comment);
    }
    friend void swap(Token &lhs, Token &rhs) noexcept { lhs.swap(rhs); }
  };

  class iterator {
   public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = uint16_t;
    using difference_type = std::ptrdiff_t;
    using pointer = uint16_t *;
    using reference = uint16_t &;

    iterator() = default;
    explicit iterator(const char *ptr) : ptr_(ptr) {}
    iterator(const iterator &x) = default;

    uint16_t pos_index() const {
      return *reinterpret_cast<const uint16_t *>(ptr_);
    }
    uint16_t value_suffix_index() const {
      return *reinterpret_cast<const uint16_t *>(ptr_ + 2);
    }
    uint16_t key_suffix_index() const {
      return *reinterpret_cast<const uint16_t *>(ptr_ + 4);
    }
    uint16_t conjugation_id() const {
      return *reinterpret_cast<const uint16_t *>(ptr_ + 6);
    }

    uint16_t operator*() const { return pos_index(); }

    void swap(iterator &x) {
      using std::swap;
      swap(ptr_, x.ptr_);
    }

    friend void swap(iterator &x, iterator &y) { x.swap(y); }

    iterator &operator++() {
      ptr_ += kTokenByteLength;
      return *this;
    }

    iterator operator++(int) {
      const char *tmp = ptr_;
      ptr_ += kTokenByteLength;
      return iterator(tmp);
    }

    iterator &operator--() {
      ptr_ -= kTokenByteLength;
      return *this;
    }

    iterator operator--(int) {
      const char *tmp = ptr_;
      ptr_ -= kTokenByteLength;
      return iterator(tmp);
    }

    iterator &operator+=(difference_type n) {
      ptr_ += n * kTokenByteLength;
      return *this;
    }

    iterator &operator-=(difference_type n) {
      ptr_ -= n * kTokenByteLength;
      return *this;
    }

    friend iterator operator+(iterator x, difference_type n) {
      return iterator(x.ptr_ + n * kTokenByteLength);
    }

    friend iterator operator+(difference_type n, iterator x) {
      return iterator(x.ptr_ + n * kTokenByteLength);
    }

    friend iterator operator-(iterator x, difference_type n) {
      return iterator(x.ptr_ - n * kTokenByteLength);
    }

    friend difference_type operator-(iterator x, iterator y) {
      return (x.ptr_ - y.ptr_) / kTokenByteLength;
    }

    friend bool operator==(iterator x, iterator y) { return x.ptr_ == y.ptr_; }
    friend bool operator!=(iterator x, iterator y) { return x.ptr_ != y.ptr_; }
    friend bool operator<(iterator x, iterator y) { return x.ptr_ < y.ptr_; }
    friend bool operator<=(iterator x, iterator y) { return x.ptr_ <= y.ptr_; }
    friend bool operator>(iterator x, iterator y) { return x.ptr_ > y.ptr_; }
    friend bool operator>=(iterator x, iterator y) { return x.ptr_ >= y.ptr_; }

   private:
    const char *ptr_ = nullptr;
  };

  using const_iterator = iterator;

  static std::unique_ptr<UserPos> CreateFromDataManager(
      const DataManager &manager);

  // Initializes the user pos from the given binary data.  The provided byte
  // data must outlive this instance.
  UserPos(absl::string_view token_array_data,
          absl::string_view string_array_data);
  UserPos(const UserPos &) = delete;
  UserPos &operator=(const UserPos &) = delete;
  virtual ~UserPos() = default;

  // Virutal for testing/mocking.
  virtual std::vector<std::string> GetPosList() const { return pos_list_; }
  virtual int GetPosListDefaultIndex() const { return pos_list_default_index_; }
  virtual bool IsValidPos(absl::string_view pos) const;
  virtual bool GetPosIds(absl::string_view pos, uint16_t *id) const;
  virtual bool GetTokens(absl::string_view key, absl::string_view value,
                         absl::string_view pos, absl::string_view locale,
                         std::vector<Token> *tokens) const;

  iterator begin() const { return iterator(token_array_data_.data()); }
  iterator end() const {
    return iterator(token_array_data_.data() + token_array_data_.size());
  }

  bool GetTokens(absl::string_view key, absl::string_view value,
                 absl::string_view pos, std::vector<Token> *tokens) const {
    return GetTokens(key, value, pos, "", tokens);
  }

 protected:
  // For testing.
  UserPos() = default;

 private:
  void InitPosList();

  absl::string_view token_array_data_;
  SerializedStringArray string_array_;
  std::vector<std::string> pos_list_;
  int pos_list_default_index_ = 0;
};

}  // namespace dictionary
}  // namespace mozc

#endif  // MOZC_DICTIONARY_USER_POS_H_
