#pragma once

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace seam::rendering {

class SharedPcmBuffer final {
public:
  using Storage = std::vector<float>;
  using iterator = Storage::iterator;
  using const_iterator = Storage::const_iterator;

  SharedPcmBuffer() : samples_(std::make_shared<Storage>()) {}
  SharedPcmBuffer(std::initializer_list<float> samples)
      : samples_(std::make_shared<Storage>(samples)) {}
  SharedPcmBuffer(const Storage& samples)
      : samples_(std::make_shared<Storage>(samples)) {}
  SharedPcmBuffer(Storage&& samples)
      : samples_(std::make_shared<Storage>(std::move(samples))) {}

  SharedPcmBuffer& operator=(std::initializer_list<float> samples) {
    samples_ = std::make_shared<Storage>(samples);
    return *this;
  }
  SharedPcmBuffer& operator=(const Storage& samples) {
    samples_ = std::make_shared<Storage>(samples);
    return *this;
  }
  SharedPcmBuffer& operator=(Storage&& samples) {
    samples_ = std::make_shared<Storage>(std::move(samples));
    return *this;
  }

  [[nodiscard]] bool empty() const noexcept { return samples_->empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return samples_->size(); }
  [[nodiscard]] std::size_t allocatedBytes() const noexcept {
    return samples_->capacity() * sizeof(float);
  }
  [[nodiscard]] const void* storageIdentity() const noexcept {
    return samples_.get();
  }
  [[nodiscard]] const float* data() const noexcept { return samples_->data(); }
  [[nodiscard]] float* data() {
    ensureUnique();
    return samples_->data();
  }
  [[nodiscard]] const float& operator[](std::size_t index) const noexcept {
    return (*samples_)[index];
  }
  [[nodiscard]] float& operator[](std::size_t index) {
    ensureUnique();
    return (*samples_)[index];
  }
  [[nodiscard]] const_iterator begin() const noexcept { return samples_->begin(); }
  [[nodiscard]] const_iterator end() const noexcept { return samples_->end(); }
  [[nodiscard]] iterator begin() {
    ensureUnique();
    return samples_->begin();
  }
  [[nodiscard]] iterator end() {
    ensureUnique();
    return samples_->end();
  }
  void assign(std::size_t count, float value) {
    samples_ = std::make_shared<Storage>(count, value);
  }
  void resize(std::size_t count) {
    ensureUnique();
    samples_->resize(count);
  }

  friend bool operator==(const SharedPcmBuffer& left,
                         const SharedPcmBuffer& right) noexcept {
    return *left.samples_ == *right.samples_;
  }

private:
  void ensureUnique() {
    if (samples_.use_count() != 1L) {
      samples_ = std::make_shared<Storage>(*samples_);
    }
  }

  std::shared_ptr<Storage> samples_;
};

}
