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

/* static */ Maybe<void> FlattenOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const int32_t start_dim = ctx->Attr<int32_t>("start_dim");
  const int32_t end_dim = ctx->Attr<int32_t>("end_dim");
  const user_op::TensorDesc& in_tensor_desc = ctx->InputTensorDesc("in", 0);
  user_op::TensorDesc* out_tensor_desc = ctx->MutOutputTensorDesc("out", 0);
  const Shape& in_shape = ExpandDimIf0D(in_tensor_desc.shape());
  CHECK_GE_OR_RETURN(start_dim, 0);
  CHECK_LT_OR_RETURN(start_dim, in_shape.NumAxes());
  const int32_t true_end_dim = end_dim < 0 ? end_dim + in_shape.NumAxes() : end_dim;
  CHECK_GE_OR_RETURN(true_end_dim, 0);
  CHECK_LT_OR_RETURN(true_end_dim, in_shape.NumAxes());
  CHECK_LE_OR_RETURN(start_dim, true_end_dim);

  *out_tensor_desc->mut_is_dynamic() = in_tensor_desc.is_dynamic();

  Shape* out_shape = out_tensor_desc->mut_shape();

  DimVector dim_vec;

  for (int i = 0; i < start_dim; ++i) { dim_vec.emplace_back(in_shape.At(i)); }
  int64_t flatten_dim = 1;
  for (int i = start_dim; i <= true_end_dim; ++i) { flatten_dim *= in_shape.At(i); }
  dim_vec.emplace_back(flatten_dim);
  for (int i = true_end_dim + 1; i < in_shape.NumAxes(); ++i) {
    dim_vec.emplace_back(in_shape.At(i));
  }

  *out_shape = Shape(dim_vec);
  CHECK_EQ_OR_RETURN(out_shape->elem_cnt(), in_shape.elem_cnt());
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> FlattenOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> FlattenOp::GetSbp(user_op::SbpContext* ctx) {
  ctx->NewBuilder().PartialSum(ctx->inputs()).PartialSum(ctx->outputs()).Build();
  const auto& in_shape = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0).shape();
  if (in_shape.NumAxes() == 0) { return Maybe<void>::Ok(); }  // 0D tensor only support b/p

  const int32_t start_dim = ctx->Attr<int32_t>("start_dim");
  const int32_t end_dim = ctx->Attr<int32_t>("end_dim");

  CHECK_GE_OR_RETURN(start_dim, 0);
  CHECK_LT_OR_RETURN(start_dim, in_shape.NumAxes());
  const int32_t true_end_dim = end_dim < 0 ? end_dim + in_shape.NumAxes() : end_dim;
  CHECK_GE_OR_RETURN(true_end_dim, 0);
  CHECK_LT_OR_RETURN(true_end_dim, in_shape.NumAxes());
  CHECK_LE_OR_RETURN(start_dim, true_end_dim);

  for (int i = 0; i <= start_dim; ++i) {
    ctx->NewBuilder().Split(user_op::OpArg("in", 0), i).Split(user_op::OpArg("out", 0), i).Build();
  }
  const int32_t diff = true_end_dim - start_dim;
  for (int i = true_end_dim + 1; i < in_shape.NumAxes(); ++i) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("in", 0), i)
        .Split(user_op::OpArg("out", 0), i - diff)
        .Build();
  }

  return Maybe<void>::Ok();
}

/* static */ Maybe<void> FlattenOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("out", 0) = ctx->InputDType("in", 0);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("flatten").SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                                                           user_op::AddOpFn AddOp) -> Maybe<void> {
  if (op.NeedGenGradTensor4OpInput("in", 0)) {
    user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
    user_op::UserOpConfWrapper reshape_grad_op =
        builder.Op("reshape_like")
            .Input("in", op.GetGradTensorWithOpOutput("out", 0))
            .Input("like", op.input("in", 0))
            .Output("out")
            .Build();
    op.BindGradTensorWithOpInput(reshape_grad_op.output("out", 0), "in", 0);
    AddOp(reshape_grad_op);
  }
  return Maybe<void>::Ok();
});

}  // namespace oneflow
