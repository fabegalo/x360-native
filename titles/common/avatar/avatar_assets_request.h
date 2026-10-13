#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace x360n::avatar {

constexpr uint32_t kAssetsPending = 0x8000000A;
constexpr uint32_t kAssetsAbort = 0x80004004;
constexpr uint32_t kAssetsInvalid = 0x80070057;
constexpr uint32_t kAssetsFailure = 0x80004005;
constexpr uint32_t kAssetsNoMemory = 0x8007000E;

struct AssetsBuffers {
  uint32_t result = 0, result_size = 0, gpu = 0, gpu_size = 0;
};

// Backend owns every guest allocation, including the final buffers. It must
// retain source data through Apply and discard partial output on Reset.
class AssetsBackend {
 public:
  virtual ~AssetsBackend() = default;
  virtual size_t Count() const = 0;
  virtual uint32_t Begin() = 0;
  virtual uint32_t Decode(size_t asset, bool& needs_apply) = 0;
  virtual uint32_t Apply(size_t asset) = 0;
  virtual uint32_t Finish() = 0;
  virtual AssetsBuffers Buffers() const = 0;
  virtual void Reset() noexcept = 0;
};

// Cooperative, single guest-thread request. Step runs one bounded phase; Cancel
// is accepted between calls. No callback/overlapped export is synthesized here.
// Published addresses are borrowed until Release or destruction of this
// request.
class AssetsRequest {
 public:
  enum class State {
    Queued,
    Decoding,
    Applying,
    Finalizing,
    Completed,
    Failed,
    Cancelled,
    Released
  };
  explicit AssetsRequest(std::unique_ptr<AssetsBackend> backend);
  ~AssetsRequest();
  AssetsRequest(const AssetsRequest&) = delete;
  AssetsRequest& operator=(const AssetsRequest&) = delete;
  uint32_t Step();
  void Cancel();
  void Release();
  State state() const { return state_; }
  uint32_t status() const { return status_; }
  AssetsBuffers result() const;

 private:
  void Fail(uint32_t status);
  std::unique_ptr<AssetsBackend> backend_;
  size_t count_, next_ = 0, apply_next_ = 0;
  std::vector<size_t> apply_;
  State state_ = State::Queued;
  uint32_t status_ = kAssetsPending;
};

}  // namespace x360n::avatar
