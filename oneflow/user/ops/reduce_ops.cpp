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
#include "oneflow/core/operator/reduce_sbp_util.h"
#include "oneflow/core/ndarray/binary_func.h"
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

Maybe<void> InferTensorDescFn(user_op::InferContext* ctx) {
  const Shape& input_shape = ctx->InputShape("input_tensor", 0);
  const auto& reduce_axes = ctx->Attr<std::vector<int32_t>>("axis");
  Shape* output_shape = ctx->MutOutputShape("output_tensor", 0);
  Stride* output_stride = ctx->MutOutputStride("output_tensor", 0);
  // For 0-dim Tensor
  if (reduce_axes.empty()) {
    *output_shape = input_shape;
  } else {
    const AxisVector reduce_axes_vec = {reduce_axes.begin(), reduce_axes.end()};
    const Shape& reduce_shape = CreateReducedShape(input_shape, reduce_axes_vec);
    const bool keepdims = ctx->Attr<bool>("keepdims");
    if (keepdims) {
      *output_shape = reduce_shape;
    } else {
      *output_shape = reduce_shape.RemoveOnes(reduce_axes_vec);
    }
  }
  *output_stride = Stride(*output_shape);
  return Maybe<void>::Ok();
}

Maybe<void> InferDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("output_tensor", 0) = ctx->InputDType("input_tensor", 0);
  return Maybe<void>::Ok();
}

Maybe<void> InferLogicalDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("output_tensor", 0) = DataType::kBool;
  return Maybe<void>::Ok();
}

template<template<typename> class binary_func>
void GeneratePartialSbp(user_op::SbpContext* ctx, int64_t axis) {
  // TODO(lixinqi)
}

template<>
void GeneratePartialSbp<BinaryFuncSum>(user_op::SbpContext* ctx, int64_t axis) {
  ctx->NewBuilder().Split(ctx->inputs(), axis).PartialSum(ctx->outputs()).Build();
  ctx->NewBuilder().PartialSum(ctx->inputs()).PartialSum(ctx->outputs()).Build();
}

template<template<typename> class binary_func>
Maybe<void> GetSbpFn(user_op::SbpContext* ctx) {
  const auto& in_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("input_tensor", 0);
  int64_t num_axes = in_tensor.shape().NumAxes();
  bool keep_dims = ctx->Attr<bool>("keepdims");
  const auto& reduce_axes = ctx->Attr<std::vector<int32_t>>("axis");
  HashSet<int32_t> conf_axes;
  ReduceSbpUtil::GetRegularAxes(num_axes, reduce_axes, &conf_axes);
  auto IsReducedAxis = ReduceSbpUtil::MakePredicatorIsReducedAxis(conf_axes, num_axes);
  int32_t num_reduced_axes = 0;
  FOR_RANGE(int64_t, i, 0, num_axes) {
    if (IsReducedAxis(i)) {
      GeneratePartialSbp<binary_func>(ctx, i);
      num_reduced_axes += 1;
    } else {
      ctx->NewBuilder()
          .Split(ctx->inputs(), i)
          .Split(ctx->outputs(), keep_dims ? i : i - num_reduced_axes)
          .Build();
    }
  }
  if (num_axes == 0) {
    ctx->NewBuilder().PartialSum(ctx->inputs()).PartialSum(ctx->outputs()).Build();
  }
  return Maybe<void>::Ok();
}

#define IMPLEMENT_REDUCE_OP_FUNCS(name, binary_func, infer_dtype_func)                   \
  /*static*/ Maybe<void> name##Op::GetSbp(user_op::SbpContext* ctx) {                    \
    return GetSbpFn<binary_func>(ctx);                                                   \
  }                                                                                      \
  /*static*/ Maybe<void> name##Op::InferLogicalTensorDesc(user_op::InferContext* ctx) {  \
    return InferTensorDescFn(ctx);                                                       \
  }                                                                                      \
  /*static*/ Maybe<void> name##Op::InferPhysicalTensorDesc(user_op::InferContext* ctx) { \
    return InferLogicalTensorDesc(ctx);                                                  \
  }                                                                                      \
  /*static*/ Maybe<void> name##Op::InferDataType(user_op::InferContext* ctx) {           \
    return infer_dtype_func(ctx);                                                        \
  }

IMPLEMENT_REDUCE_OP_FUNCS(ReduceAny, BinaryFuncAny, InferLogicalDataType)
IMPLEMENT_REDUCE_OP_FUNCS(ReduceAll, BinaryFuncAll, InferLogicalDataType)
IMPLEMENT_REDUCE_OP_FUNCS(ReduceMin, BinaryFuncMin, oneflow::InferDataType)
IMPLEMENT_REDUCE_OP_FUNCS(ReduceMax, BinaryFuncMax, oneflow::InferDataType)
IMPLEMENT_REDUCE_OP_FUNCS(ReduceSum, BinaryFuncSum, oneflow::InferDataType)
IMPLEMENT_REDUCE_OP_FUNCS(ReduceProd, BinaryFuncProd, oneflow::InferDataType)
#undef IMPLEMENT_REDUCE_OP_FUNCS

REGISTER_USER_OP_GRAD("reduce_sum")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               user_op::AddOpFn AddOp) -> Maybe<void> {
      if (op.NeedGenGradTensor4OpInput("input_tensor", 0)) {
        const auto& axes = op.attr<std::vector<int32_t>>("axis");
        user_op::UserOpConfWrapperBuilder builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper reduce_sum_grad_op =
            builder.Op("broadcast_like")
                .Input("x", op.GetGradTensorWithOpOutput("output_tensor", 0))
                .Input("like", op.input("input_tensor", 0))
                .Attr("broadcast_axes", axes)
                .Output("y")
                .Build();
        op.BindGradTensorWithOpInput(reduce_sum_grad_op.output("y", 0), "input_tensor", 0);
        AddOp(reduce_sum_grad_op);
      }
      return Maybe<void>::Ok();
    });

Maybe<void> GenerateBackwardOpConf4ReduceMaxMin(const user_op::UserOpWrapper& op,
                                                user_op::AddOpFn AddOp) {
  if (op.NeedGenGradTensor4OpInput("input_tensor", 0)) {
    const auto& axes = op.attr<std::vector<int32_t>>("axis");

    user_op::UserOpConfWrapperBuilder broadcast_out_builder(op.op_name() + "_grad_broadcast_out");
    user_op::UserOpConfWrapper broadcast_out_op = broadcast_out_builder.Op("broadcast_like")
                                                      .Input("x", op.output("output_tensor", 0))
                                                      .Input("like", op.input("input_tensor", 0))
                                                      .Attr("broadcast_axes", axes)
                                                      .Output("y")
                                                      .Build();
    AddOp(broadcast_out_op);

    user_op::UserOpConfWrapperBuilder broadcast_eq_builder(op.op_name() + "_grad_broadcast_eq");
    user_op::UserOpConfWrapper broadcast_eq_op = broadcast_eq_builder.Op("broadcast_equal")
                                                     .Input("x", op.input("input_tensor", 0))
                                                     .Input("y", broadcast_out_op.output("y", 0))
                                                     .Output("z")
                                                     .Build();
    AddOp(broadcast_eq_op);

    user_op::UserOpConfWrapperBuilder cast_mask_builder(op.op_name() + "_grad_cast_mask");
    user_op::UserOpConfWrapper cast_mask_op = cast_mask_builder.Op("cast_like")
                                                  .Input("in", broadcast_eq_op.output("z", 0))
                                                  .Input("dtype_like", op.input("input_tensor", 0))
                                                  .Output("out")
                                                  .Build();
    AddOp(cast_mask_op);

    user_op::UserOpConfWrapperBuilder reduce_sum_mask_builder(op.op_name()
                                                              + "_grad_reduce_sum_mask");
    user_op::UserOpConfWrapper reduce_sum_mask_op =
        reduce_sum_mask_builder.Op("reduce_sum")
            .Input("input_tensor", cast_mask_op.output("out", 0))
            .Output("output_tensor")
            .Attr("axis", axes)
            .Attr("keepdims", op.attr<bool>("keepdims"))
            .Build();
    AddOp(reduce_sum_mask_op);

    user_op::UserOpConfWrapperBuilder divide_count_builder(op.op_name() + "_grad_divide_count");
    user_op::UserOpConfWrapper divide_count_op =
        divide_count_builder.Op("broadcast_div")
            .Input("x", op.GetGradTensorWithOpOutput("output_tensor", 0))
            .Input("y", reduce_sum_mask_op.output("output_tensor", 0))
            .Output("z")
            .Build();
    AddOp(divide_count_op);

    user_op::UserOpConfWrapperBuilder broadcast_divided_dy_builder(op.op_name()
                                                                   + "_grad_broadcast_divided_dy");
    user_op::UserOpConfWrapper broadcast_divided_dy_op =
        broadcast_divided_dy_builder.Op("broadcast_like")
            .Input("x", divide_count_op.output("z", 0))
            .Input("like", op.input("input_tensor", 0))
            .Attr("broadcast_axes", axes)
            .Output("y")
            .Build();
    AddOp(broadcast_divided_dy_op);

    user_op::UserOpConfWrapperBuilder multiply_mask_builder(op.op_name() + "_grad_multiply_mask");
    user_op::UserOpConfWrapper multiply_mask_op =
        multiply_mask_builder.Op("broadcast_mul")
            .Input("x", broadcast_divided_dy_op.output("y", 0))
            .Input("y", cast_mask_op.output("out", 0))
            .Output("z")
            .Build();
    AddOp(multiply_mask_op);
    op.BindGradTensorWithOpInput(multiply_mask_op.output("z", 0), "input_tensor", 0);
  }
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("reduce_max").SetGenBackwardOpConfFn(GenerateBackwardOpConf4ReduceMaxMin);
REGISTER_USER_OP_GRAD("reduce_min").SetGenBackwardOpConfFn(GenerateBackwardOpConf4ReduceMaxMin);

}  // namespace oneflow
