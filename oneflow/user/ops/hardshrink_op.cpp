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
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

/* static */ Maybe<void> HardShrinkOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  *ctx->MutOutputShape("out", 0) = ctx->InputShape("in", 0);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> HardShrinkOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> HardShrinkOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
  FOR_RANGE(int64_t, i, 0, in_tensor.shape().NumAxes()) {
    ctx->NewBuilder().Split(user_op::OpArg("in", 0), i).Split(user_op::OpArg("out", 0), i).Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> HardShrinkOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("out", 0) = ctx->InputDType("in", 0);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> HardShrinkGradOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const Shape& y_shape = ctx->InputShape("y", 0);
  const Shape& dy_shape = ctx->InputShape("dy", 0);
  Shape* dx_shape = ctx->MutOutputShape("dx", 0);
  CHECK_OR_RETURN(dy_shape == y_shape) << "The shape of y_grad and y must be same.";
  *dx_shape = dy_shape;
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> HardShrinkGradOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> HardShrinkGradOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& y_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("y", 0);
  FOR_RANGE(int64_t, i, 0, y_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("y", 0), i)
        .Split(user_op::OpArg("dy", 0), i)
        .Split(user_op::OpArg("dx", 0), i)
        .Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> HardShrinkGradOp::InferDataType(user_op::InferContext* ctx) {
  CHECK_EQ_OR_RETURN(ctx->InputDType("dy", 0), ctx->InputDType("y", 0))
      << "The dtype of y_grad and y must be same.";
  *ctx->MutOutputDType("dx", 0) = ctx->InputDType("y", 0);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("hardshrink")
    .SetBackwardOpConfGenFn([](user_op::BackwardOpConfContext* ctx) -> Maybe<void> {
      const auto hardshrink_grad_op_name = ctx->FwOp().op_name() + "_grad";
      ctx->DefineOp(hardshrink_grad_op_name, [&ctx](user_op::BackwardOpBuilder& builder) {
        return builder.OpTypeName("hardshrink_grad")
            .InputBind("y", ctx->FwOp().output("y", 0))
            .InputBind("dy", ctx->FwOp().output_grad("out", 0))
            .Attr<double>("lambd", ctx->FwOp().attr<double>("lambd"))
            .Output("dx")
            .Build();
      });
      ctx->FwOp().InputGradBind(user_op::OpArg("in", 0),
                                [&ctx, &hardshrink_grad_op_name]() -> const std::string& {
                                  return ctx->GetOp(hardshrink_grad_op_name).output("dx", 0);
                                });
      return Maybe<void>::Ok();
    });
}  // namespace oneflow
