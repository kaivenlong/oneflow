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

/* static */ Maybe<void> AmpWhiteIdentityOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const user_op::TensorDesc& in = ctx->InputTensorDesc("in", 0);
  user_op::TensorDesc* out = ctx->MutOutputTensorDesc("out", 0);
  *out->mut_shape() = in.shape();
  *out->mut_is_dynamic() = in.is_dynamic();
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> AmpWhiteIdentityOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> AmpWhiteIdentityOp::GetSbp(user_op::SbpContext* ctx) {
  const auto& in = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
  for (int i = 0; i < in.shape().NumAxes(); ++i) {
    ctx->NewBuilder().Split(ctx->inputs(), i).Split(ctx->outputs(), i).Build();
  }
  ctx->NewBuilder().PartialSum(ctx->inputs()).PartialSum(ctx->outputs()).Build();
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> AmpWhiteIdentityOp::InferDataType(user_op::InferContext* ctx) {
  const user_op::TensorDesc& in = ctx->InputTensorDesc("in", 0);
  user_op::TensorDesc* out = ctx->MutOutputTensorDesc("out", 0);
  *out->mut_data_type() = in.data_type();
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("amp_white_identity")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               user_op::AddOpFn AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("in", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper grad_op =
            builder.Op("amp_white_identity")
                .Input("in", op.GetGradTensorWithOpOutput("out", 0))
                .Output("out")
                .Build();
        op.BindGradTensorWithOpInput(grad_op.output("out", 0), "in", 0);
        AddOp(grad_op);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
