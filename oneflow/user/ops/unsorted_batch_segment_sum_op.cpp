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

/*static*/ Maybe<void> UnsortedBatchSegmentSumOp::GetSbp(user_op::SbpContext* ctx) {
  const int64_t segment_ids_num_axes =
      ctx->LogicalTensorDesc4InputArgNameAndIndex("segment_ids", 0).shape().NumAxes();
  if (segment_ids_num_axes > 1) {
    FOR_RANGE(int64_t, i, 0, segment_ids_num_axes - 1) {
      ctx->NewBuilder()
          .Split(user_op::OpArg("segment_ids", 0), i)
          .Split(user_op::OpArg("data", 0), i)
          .Split(user_op::OpArg("out", 0), i)
          .Build();
    }
  }
  ctx->NewBuilder()
      .Broadcast(user_op::OpArg("segment_ids", 0))
      .PartialSum(user_op::OpArg("data", 0))
      .PartialSum(user_op::OpArg("out", 0))
      .Build();
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> UnsortedBatchSegmentSumOp::InferLogicalTensorDesc(
    user_op::InferContext* ctx) {
  const user_op::TensorDesc& data = ctx->InputTensorDesc("data", 0);
  const user_op::TensorDesc& segment_ids = ctx->InputTensorDesc("segment_ids", 0);
  CHECK_GE_OR_RETURN(segment_ids.shape().NumAxes(), 1);
  CHECK_GE_OR_RETURN(data.shape().NumAxes(), segment_ids.shape().NumAxes());
  CHECK_EQ_OR_RETURN(segment_ids.is_dynamic(), data.is_dynamic());
  const int64_t num_segments = ctx->Attr<int64_t>("num_segments");
  CHECK_GE_OR_RETURN(num_segments, 1);
  user_op::TensorDesc* out = ctx->MutOutputTensorDesc("out", 0);

  FOR_RANGE(int64_t, i, 0, segment_ids.shape().NumAxes() - 1) {
    CHECK_EQ_OR_RETURN(segment_ids.shape().At(i), data.shape().At(i));
  }

  DimVector dim_vec(data.shape().dim_vec());
  dim_vec.at(segment_ids.shape().NumAxes() - 1) = num_segments;
  *out->mut_shape() = Shape(dim_vec);
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> UnsortedBatchSegmentSumOp::InferPhysicalTensorDesc(
    user_op::InferContext* ctx) {
  return InferLogicalTensorDesc(ctx);
}
/*static*/ Maybe<void> UnsortedBatchSegmentSumOp::InferDataType(user_op::InferContext* ctx) {
  const user_op::TensorDesc& data = ctx->InputTensorDesc("data", 0);
  const user_op::TensorDesc& segment_ids = ctx->InputTensorDesc("segment_ids", 0);
  user_op::TensorDesc* out = ctx->MutOutputTensorDesc("out", 0);
  CHECK_OR_RETURN(IsIndexDataType(segment_ids.data_type()));
  *out->mut_data_type() = data.data_type();
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> UnsortedBatchSegmentSumOp::ModifyInputArg(
    const GetInputArgModifier& GetInputArgModifierFn, const user_op::UserOpConfWrapper&) {
  user_op::InputArgModifier* segment_ids_modifier = GetInputArgModifierFn("segment_ids", 0);
  CHECK_NOTNULL_OR_RETURN(segment_ids_modifier);
  segment_ids_modifier->set_requires_grad(false);
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("unsorted_batch_segment_sum")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                               user_op::AddOpFn AddOp) -> Maybe<void> {
      bool need_grad_data = op.NeedGenGradTensor4OpInput("data", 0);
      if (need_grad_data) {
        user_op::UserOpConfWrapperBuilder data_grad_builder(op.op_name() + "_grad");
        user_op::UserOpConfWrapper data_grad_op =
            data_grad_builder.Op("batch_gather")
                .Input("in", op.GetGradTensorWithOpOutput("out", 0))
                .Input("indices", op.input("segment_ids", 0))
                .Output("out")
                .Build();
        op.BindGradTensorWithOpInput(data_grad_op.output("out", 0), "data", 0);
        AddOp(data_grad_op);
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
