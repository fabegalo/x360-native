#include "avatar_assets_request.h"

#include <utility>

namespace x360n::avatar {

AssetsRequest::AssetsRequest(std::unique_ptr<AssetsBackend> backend)
    : backend_(std::move(backend)), count_(backend_ ? backend_->Count() : 0) {}
AssetsRequest::~AssetsRequest() { Release(); }

void AssetsRequest::Fail(uint32_t status) {
  status_ = status;
  state_ = State::Failed;
  if (backend_) backend_->Reset();
}

uint32_t AssetsRequest::Step() {
  uint32_t hr = 0;
  switch (state_) {
    case State::Queued:
      if (!backend_ || !count_ || count_ > 32) {
        Fail(kAssetsInvalid);
        break;
      }
      hr = backend_->Begin();
      if (!hr) state_ = State::Decoding;
      break;
    case State::Decoding: {
      bool needs_apply = false;
      hr = backend_->Decode(next_, needs_apply);
      if (!hr) {
        if (needs_apply) apply_.push_back(next_);
        if (++next_ == count_) state_ = State::Applying;
      }
      break;
    }
    case State::Applying:
      if (apply_next_ < apply_.size())
        hr = backend_->Apply(apply_[apply_next_++]);
      if (!hr && apply_next_ == apply_.size()) state_ = State::Finalizing;
      break;
    case State::Finalizing:
      hr = backend_->Finish();
      if (!hr) {
        const auto buffers = backend_->Buffers();
        if (!buffers.result || !buffers.gpu || !buffers.result_size ||
            !buffers.gpu_size)
          hr = kAssetsFailure;
        else {
          status_ = 0;
          state_ = State::Completed;
        }
      }
      break;
    default:
      return status_;  // Polling terminal states cannot execute guest code.
  }
  if (hr) Fail(hr);
  return status_;
}

void AssetsRequest::Cancel() {
  if (state_ != State::Queued && state_ != State::Decoding &&
      state_ != State::Applying && state_ != State::Finalizing)
    return;
  if (backend_) backend_->Reset();
  status_ = kAssetsAbort;
  state_ = State::Cancelled;
}

void AssetsRequest::Release() {
  if (state_ == State::Released) return;
  if (backend_) backend_->Reset();
  if (status_ == kAssetsPending) status_ = kAssetsAbort;
  state_ = State::Released;
}

AssetsBuffers AssetsRequest::result() const {
  return state_ == State::Completed ? backend_->Buffers() : AssetsBuffers{};
}

}  // namespace x360n::avatar
