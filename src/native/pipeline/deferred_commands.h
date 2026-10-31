#pragma once
#include <chrono>
#include <functional>
#include <future>
#include <utility>
#include <vector>
namespace x360n::native {
// Command recording that tolerates pipelines still being created.
//
// Commands go straight to the open command list (one branch per command)
// until a pipeline that is not ready yet is bound. From then on every command
// is kept, in order, with copies of its arguments and replayed into the same
// list by Replay() before the list is closed. Nothing is skipped, merged or
// reordered, so the submitted list is identical to synchronous recording; only
// the moment the driver sees the commands moves to the submission.
template <class List>
class DeferredCommands {
 public:
  using Op = std::function<void(List*)>;
  void Attach(List* list) { list_ = list; }
  List* list() const { return list_; }
  bool deferred() const { return !ops_.empty(); }
  size_t deferred_count() const { return ops_.size(); }
  // f(List*) must own every argument by value: it may run after the caller's
  // stack frame is gone.
  template <class F>
  void Record(F&& f) {
    if (ops_.empty()) f(list_);
    else ops_.emplace_back(std::forward<F>(f));
  }
  // bind(List*, const T&) receives the created pipeline.
  template <class T, class Bind>
  void BindPending(const std::shared_future<T>& pipeline, Bind bind) {
    if (ops_.empty() && pipeline.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      bind(list_, pipeline.get());
      return;
    }
    ops_.emplace_back([pipeline, bind](List* list) { bind(list, pipeline.get()); });
  }
  // Emits the kept commands in order. A pipeline whose creation failed
  // rethrows its error here, as the synchronous path threw at the draw.
  void Replay() {
    if (ops_.empty()) return;
    auto ops = std::move(ops_);
    ops_.clear();
    for (auto& op : ops) op(list_);
    // Keep the capacity: deferral recurs while new pipelines appear.
    ops.clear();
    ops_ = std::move(ops);
  }
 private:
  List* list_ = nullptr;
  std::vector<Op> ops_;
};
}
