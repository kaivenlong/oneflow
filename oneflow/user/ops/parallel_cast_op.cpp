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
#include "oneflow/core/operator/operator.h"
#include "oneflow/core/framework/op_generated.h"

namespace oneflow {

/*static*/ Maybe<void> ParallelCastOp::GetSbp(user_op::SbpContext* ctx) {
  return user_op::GetSbpFnUtil::DefaultBroadcastToBroadcast(ctx);
}
/*static*/ Maybe<void> ParallelCastOp::InferLogicalTensorDesc(user_op::InferContext* ctx) {
  *ctx->MutOutputShape("out", 0) = ctx->InputShape("in", 0);
  *ctx->MutOutputIsDynamic("out", 0) = ctx->InputIsDynamic("in", 0);
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> ParallelCastOp::InferPhysicalTensorDesc(user_op::InferContext* ctx) {
  return ParallelCastOp::InferLogicalTensorDesc(ctx);
}
/*static*/ Maybe<void> ParallelCastOp::InferDataType(user_op::InferContext* ctx) {
  *ctx->MutOutputDType("out", 0) = ctx->InputDType("in", 0);
  return Maybe<void>::Ok();
}
/*static*/ Maybe<void> ParallelCastOp::InferSbpSignature(user_op::InferSbpSignatureFnContext* ctx) {
  auto* bn2sbp = ctx->mutable_sbp_signature()->mutable_bn_in_op2sbp_parallel();
  const std::string& ibn = GenRepeatedBn("in", 0);
  const std::string& obn = GenRepeatedBn("out", 0);
  const auto& sbp_parallel_str = ctx->Attr<std::string>("sbp_parallel");
  if (sbp_parallel_str.empty()) {
    const auto& sbp_parallel = ctx->SbpParallelHint4InputArgNameAndIndex("in", 0);
    (*bn2sbp)[ibn] = sbp_parallel;
    (*bn2sbp)[obn] = sbp_parallel;
  } else {
    SbpParallel sbp_parallel;
    CHECK_OR_RETURN(ParseSbpParallelFromString(sbp_parallel_str, &sbp_parallel))
        << "invalid sbp_parallel: " << sbp_parallel_str;
    if (sbp_parallel.has_split_parallel()) {
      int64_t split_axis = sbp_parallel.split_parallel().axis();
      const auto& in_desc = ctx->LogicalTensorDesc4InputArgNameAndIndex("in", 0);
      int64_t num_axes = in_desc.shape().NumAxes();
      CHECK_GE_OR_RETURN(split_axis, 0);
      CHECK_LT_OR_RETURN(split_axis, num_axes);
    }
    (*bn2sbp)[ibn] = sbp_parallel;
    (*bn2sbp)[obn] = sbp_parallel;
  }
  return Maybe<void>::Ok();
}

REGISTER_USER_OP_GRAD("parallel_cast")
    .SetBackwardOpConfGenFn([](user_op::BackwardOpConfContext* ctx) -> Maybe<void> {
      if (ctx->FwOp().NeedGenGradTensor4OpInput("in", 0)) {
        const auto& grad_sbp_parallel_str = ctx->FwOp().attr<std::string>("grad_sbp_parallel");
        if (grad_sbp_parallel_str.empty()) {
          ctx->FwOp().BindGradTensorWithOpInput(ctx->FwOp().GetGradTensorWithOpOutput("out", 0),
                                                "in", 0);
        } else {
          CHECK_OR_RETURN(IsValidSbpParallelString(grad_sbp_parallel_str));
          const std::string grad_op_name = "System-AutoGrad-" + ctx->FwOp().op_name();
          ctx->DefineOp(grad_op_name, [&](user_op::BackwardOpBuilder& builder) {
            return builder.OpTypeName("parallel_cast")
                .InputBind("in", ctx->FwOp().output_grad("out", 0))
                .Output("out")
                .Attr("sbp_parallel", grad_sbp_parallel_str)
                .Build();
          });
          ctx->FwOp().InputGradBind(user_op::OpArg("in", 0), [&]() -> const std::string& {
            return ctx->GetOp(grad_op_name).output("out", 0);
          });
        }
      }
      return Maybe<void>::Ok();
    });

}  // namespace oneflow
