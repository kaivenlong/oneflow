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

/* static */ Maybe<void> FakeQuantizationOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  const Shape& in_shape = ctx->InputShape("in", 0);
  const Shape& scale_shape = ctx->InputShape("scale", 0);
  const Shape& zero_point_shape = ctx->InputShape("zero_point", 0);

  // NOTE(Liang Depeng): scale_shape->elem_cnt() > 1 means per-channel quantization for
  // convolution weights.
  if (scale_shape.elem_cnt() > 1) {
    CHECK_EQ_OR_RETURN(scale_shape.elem_cnt(), in_shape.At(0));
    CHECK_EQ_OR_RETURN(zero_point_shape.elem_cnt(), in_shape.At(0));
  }

  *ctx->MutOutputShape("out", 0) = in_shape;
  return Maybe<void>::Ok();
}

/*static*/ Maybe<void> FakeQuantizationOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}

/* static */ Maybe<void> FakeQuantizationOp::GetSbp(user_op::SbpContext* ctx) {
  const user_op::TensorDesc& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
  const Shape& logical_scale_shape =
      ctx->LogicalTensorDesc4InputArgNameAndIndex("scale", 0).shape();
  ctx->NewBuilder()
      .Broadcast(user_op::OpArg("in", 0))
      .Broadcast(user_op::OpArg("scale", 0))
      .Broadcast(user_op::OpArg("zero_point", 0))
      .Broadcast(user_op::OpArg("out", 0))
      .Build();
  if (logical_scale_shape.elem_cnt() > 1) {
    // NOTE(Liang Depeng): only consider convolution weight per-channel quantization
    ctx->NewBuilder()
        .Split(user_op::OpArg("in", 0), 0)
        .Split(user_op::OpArg("scale", 0), 0)
        .Split(user_op::OpArg("zero_point", 0), 0)
        .Split(user_op::OpArg("out", 0), 0)
        .Build();
  } else {
    // NOTE(Liang Depeng): the sbp signature of per-layer quantization is the same as eltwise
    // ops
    ctx->NewBuilder()
        .Split(user_op::OpArg("in", 0), 0)
        .Broadcast(user_op::OpArg("scale", 0))
        .Broadcast(user_op::OpArg("zero_point", 0))
        .Split(user_op::OpArg("out", 0), 0)
        .Build();
  }
  FOR_RANGE(int64_t, i, 1, in_tensor.shape().NumAxes()) {
    ctx->NewBuilder()
        .Split(user_op::OpArg("in", 0), i)
        .Broadcast(user_op::OpArg("scale", 0))
        .Broadcast(user_op::OpArg("zero_point", 0))
        .Split(user_op::OpArg("out", 0), i)
        .Build();
  }
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> FakeQuantizationOp::ModifyInputArg(
    const GetInputArgModifier& GetInputArgModifierFn, const user_op::UserOpConfWrapper& conf) {
  user_op::InputArgModifier* scale = GetInputArgModifierFn("scale", 0);
  CHECK_OR_RETURN(scale != nullptr);
  scale->set_requires_grad(false);

  user_op::InputArgModifier* zero_point = GetInputArgModifierFn("zero_point", 0);
  CHECK_OR_RETURN(zero_point != nullptr);
  zero_point->set_requires_grad(false);
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> FakeQuantizationOp::CheckAttr(const user_op::UserOpDefWrapper& def,
                                                       const user_op::UserOpConfWrapper& conf) {
  const int32_t quantization_bit = conf.attr<int32_t>("quantization_bit");
  CHECK_GT_OR_RETURN(quantization_bit, 1);
  CHECK_LE_OR_RETURN(quantization_bit, 8);

  std::string quantization_scheme = conf.attr<std::string>("quantization_scheme");
  CHECK_OR_RETURN(quantization_scheme == "symmetric" || quantization_scheme == "affine");

  std::string quantization_formula = conf.attr<std::string>("quantization_formula");
  CHECK_OR_RETURN(quantization_formula == "google" || quantization_formula == "cambricon");
  return Maybe<void>::Ok();
}

/* static */ Maybe<void> FakeQuantizationOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("out", 0) = ctx->InputDType("in", 0);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("fake_quantization")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               user_op::AddOpFn AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("in", 0)) {
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper identity_op =
            builder.Op("identity")
                .Input("in", op.GetGradTensorWithOpOutput("out", 0))
                .Output("out")
                .Build();
        op.BindGradTensorWithOpInput(identity_op.output("out", 0), "in", 0);
        AddOp(identity_op);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
