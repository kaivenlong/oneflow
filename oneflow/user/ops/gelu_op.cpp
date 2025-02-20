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

/*static*/ auto GeluOp::InferLogicalTensorDesc(user_op::InferContext* ctx) -> Maybe<void> {
  const Shape& in_shape = ctx->InputShape("in", 0);
  Shape* out_shape = ctx->MutOutputShape("out", 0);
  *out_shape = in_shape;
  return Maybe<void>::Ok();
}
/*static*/ auto GeluOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) -> Maybe<void> {
  return GeluOp::InferLogicalTensorDesc(ctx);
}
/*static*/ auto GeluOp::GetSbp(user_op::SbpContext* ctx) -> Maybe<void> {
  const user_op::TensorDesc& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
  FOR_RANGE(int64_t, i, 0, in_tensor.shape().NumAxes()) {
    ctx->NewBuilder().Split(user_op::OpArg("in", 0), i).Split(user_op::OpArg("out", 0), i).Build();
  }
  return Maybe<void>::Ok();
}
/*static*/ auto GeluOp::InferDataType(user_op::InferContext* ctx) -> Maybe<void> {
  *ctx->MutOutputDType("out", 0) = ctx->InputDType("in", 0);
  return Maybe<void>::Ok();
}

/*static*/ auto GeluGradOp::InferLogicalTensorDesc(user_op::InferContext* ctx) -> Maybe<void> {
  const Shape& x_shape = ctx->InputShape("x", 0);
  const Shape& dy_shape = ctx->InputShape("dy", 0);
  Shape* dx_shape = ctx->MutOutputShape("dx", 0);
  CHECK_OR_RETURN(dy_shape == x_shape);
  *dx_shape = dy_shape;
  return Maybe<void>::Ok();
}
/*static*/ auto GeluGradOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) -> Maybe<void> {
  return GeluGradOp::InferLogicalTensorDesc(ctx);
}
/*static*/ auto GeluGradOp::GetSbp(user_op::SbpContext* ctx) -> Maybe<void> {
  const user_op::TensorDesc& x_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("x", 0);
  FOR_RANGE(int64_t, i, 0, x_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("x", 0), i)
        .Split(user_op::OpArg("dy", 0), i)
        .Split(user_op::OpArg("dx", 0), i)
        .Build();
  }
  ctx->NewBuilder()
      .Broadcast(user_op::OpArg("x", 0))
      .PartialSum(user_op::OpArg("dy", 0))
      .PartialSum(user_op::OpArg("dx", 0))
      .Build();
  return Maybe<void>::Ok();
}
/*static*/ auto GeluGradOp::InferDataType(user_op::InferContext* ctx) -> Maybe<void> {
  CHECK_EQ_OR_RETURN(ctx->InputDType("x", 0), ctx->InputDType("dy", 0));
  *ctx->MutOutputDType("dx", 0) = ctx->InputDType("x", 0);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("gelu").SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                                                        user_op::AddOpFn AddOp) -> Maybe<void> {
  if (op.NeedGenGradTensor4OpInput("in", 0)) {
    user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
    user_op::UserOpConfWrapper grad_op = builder.Op("gelu_grad")
                                             .Input("x", op.input("in", 0))
                                             .Input("dy", op.GetGradTensorWithOpOutput("out", 0))
                                             .Output("dx")
                                             .Build();
    op.BindGradTensorWithOpInput(grad_op.output("dx", 0), "in", 0);
    AddOp(grad_op);
  }
  return Maybe<void>::Ok();
});

}  // namespace oneflow
