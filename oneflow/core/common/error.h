/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#ifndef ONEFLOW_CORE_COMMON_ERROR_H_
#define ONEFLOW_CORE_COMMON_ERROR_H_

#include <sstream>
#include <vector>
#include <functional>
#include "oneflow/core/common/error.pb.h"
#include "oneflow/core/common/symbol.h"
#include "oneflow/core/common/small_vector.h"

namespace oneflow {

class ErrorStackFrame final {
 public:
  ErrorStackFrame(const ErrorStackFrame&) = default;
  ErrorStackFrame(const std::string& file, int64_t line, const std::string& function)
      : file_(file), line_(line), function_(function), code_text_() {}
  ErrorStackFrame(const std::string& file, int64_t line, const std::string& function,
                  const std::string& code_text)
      : file_(file), line_(line), function_(function), code_text_(code_text) {}

  bool operator==(const ErrorStackFrame& other) const {
    return this->file_ == other.file_ && this->line_ == other.line_
           && this->function_ == other.function_ && this->code_text_ == other.code_text_;
  }

  const std::string& file() const { return file_; }
  int64_t line() const { return line_; }
  const std::string& function() const { return function_; }
  const std::string& code_text() const { return code_text_; }

  std::string DebugString() const {
    return file_ + ":" + std::to_string(line_) + " " + function_ + "\n\t" + code_text_ + "\n";
  }

 private:
  std::string file_;
  int64_t line_;
  std::string function_;
  std::string code_text_;
};

}  // namespace oneflow

namespace std {

template<>
struct hash<::oneflow::ErrorStackFrame> final {
  size_t operator()(const ::oneflow::ErrorStackFrame& frame) const {
    const auto& string_hash = std::hash<std::string>();
    return string_hash(frame.file()) ^ std::hash<int64_t>()(frame.line())
           ^ string_hash(frame.function()) ^ string_hash(frame.code_text());
  }
};

}  // namespace std

namespace oneflow {

class StackedError final {
 public:
  StackedError();
  StackedError(const StackedError&) = default;

  constexpr static int kStackReservedSize = 16;
  using FrameVector = small_vector<Symbol<ErrorStackFrame>, kStackReservedSize>;

  const ErrorProto* operator->() const { return error_proto().get(); }
  ErrorProto* operator->() { return mut_error_proto(); }

  // Getters
  const FrameVector& stack_frame() const { return stack_frame_; }
  const std::shared_ptr<const ErrorProto>& error_proto() const { return error_proto_; }
  std::string DebugString() const {
    std::string str;
    for (const auto& frame : stack_frame()) { str += frame->DebugString() + "\n"; }
    str += error_proto()->DebugString();
    return str;
  }

  // Setters
  void add_stack_frame(Symbol<ErrorStackFrame> error_frame) { stack_frame_.push_back(error_frame); }
  ErrorProto* mut_error_proto() { return const_cast<ErrorProto*>(error_proto_.get()); }

 private:
  FrameVector stack_frame_;
  std::shared_ptr<const ErrorProto> error_proto_;
};

class Error final {
 public:
  Error(const std::shared_ptr<StackedError>& stacked_error) : stacked_error_(stacked_error) {}
  Error(const Error&) = default;
  ~Error() = default;

  std::shared_ptr<StackedError> stacked_error() const { return stacked_error_; }
  const ErrorProto* operator->() const { return stacked_error_->error_proto().get(); }
  ErrorProto* operator->() { return stacked_error_->mut_error_proto(); }
  operator std::string() const;
  void Assign(const Error& other) { stacked_error_ = other.stacked_error_; }
  void Merge(const Error& other);

  Error&& AddStackFrame(Symbol<ErrorStackFrame> error_stack_frame);

  static Error Ok();
  static Error ProtoParseFailedError();
  static Error JobSetEmptyError();
  static Error DeviceTagNotFoundError();
  static Error InvalidValueError();
  static Error IndexError();
  static Error TypeError();
  static Error TimeoutError();
  static Error JobNameExistError();
  static Error JobNameEmptyError();
  static Error JobNameNotEqualError();
  static Error NoJobBuildAndInferCtxError();
  static Error JobConfFrozenError();
  static Error JobConfNotSetError();
  static Error JobConfRepeatedSetError();
  static Error JobTypeNotSetError();
  static Error LogicalBlobNameNotExistError();
  static Error LogicalBlobNameExistError();
  static Error LogicalBlobNameInvalidError();
  static Error OpNameExistError();
  static Error OpConfDeviceTagNoSetError();
  static Error PlacementError();
  static Error BlobSplitAxisInferError();
  static Error UnknownJobBuildAndInferError();
  static Error CheckFailedError();
  static Error ValueNotFoundError();
  static Error TodoError();
  static Error UnimplementedError();
  static Error RuntimeError();
  static Error OutOfMemoryError();
  static Error BoxingNotSupportedError();
  static Error MemoryZoneOutOfMemoryError(int64_t machine_id, int64_t mem_zone_id, uint64_t calc,
                                          uint64_t available, const std::string& device_type);
  static Error OpKernelNotFoundError(const std::vector<std::string>& error_msgs);
  static Error MultipleOpKernelsMatchedError(const std::vector<std::string>& error_msgs);
  static Error LossBlobNotFoundError();

  static Error RwMutexedObjectNotFoundError();

  // gradient
  static Error GradientFunctionNotFoundError();

  // symbol
  static Error SymbolIdUninitializedError();

  static Error CompileOptionWrongError();

  static Error InputDeviceNotMatchError();

 private:
  std::shared_ptr<StackedError> stacked_error_;
};

void ThrowError(const std::shared_ptr<StackedError>& error);
const std::shared_ptr<StackedError>& ThreadLocalError();

template<typename T>
Error& operator<<(Error& error, const T& x) {
  std::ostringstream ss;
  ss << x;
  error->set_msg(error->msg() + ss.str());
  return error;
}

// r-value reference is used to supporting expressions like `Error() << "invalid value"`
template<typename T>
Error&& operator<<(Error&& error, const T& x) {
  error << x;
  return std::move(error);
}

template<>
inline Error&& operator<<(Error&& error, const std::stringstream& x) {
  error << x.str();
  return std::move(error);
}

template<>
inline Error&& operator<<(Error&& error, const std::ostream& x) {
  error << x.rdbuf();
  return std::move(error);
}

template<>
inline Error&& operator<<(Error&& error, const Error& other) {
  error.Merge(other);
  return std::move(error);
}

extern const char* kOfBugIssueUploadPrompt;

}  // namespace oneflow

#define PRINT_BUG_PROMPT_AND_ABORT() LOG(FATAL) << kOfBugIssueUploadPrompt

#endif  // ONEFLOW_CORE_COMMON_ERROR_H_
