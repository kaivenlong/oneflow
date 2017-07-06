#include "oneflow/core/actor/model_diff_accumulate_actor.h"

namespace oneflow {

void MdDiffAccActor::Init(const TaskProto& task_proto,
                          const ThreadCtx& thread_ctx) {
  CompActor::Init(task_proto, thread_ctx);
  if (thread_ctx.cpu_stream) {
    MemsetFunc = &KernelUtil<DeviceType::kCPU, float>::Memset;
    mut_device_ctx().reset(new CpuDeviceCtx(thread_ctx.cpu_stream));
  } else {
    MemsetFunc = &KernelUtil<DeviceType::kGPU, float>::Memset;
    mut_device_ctx().reset(new CudaDeviceCtx(cuda_handle_.cuda_stream(),
                                             cuda_handle_.cublas_handle(),
                                             cuda_handle_.cudnn_handle()));
  }
  OF_SET_MSG_HANDLE(&MdDiffAccActor::HandleNormal);
  ForEachCurWriteableRegst(
      [this](Regst* regst) { model_diff_acc_cnt_[regst] = 0; });
}

int MdDiffAccActor::HandleNormal(const ActorMsg& msg) {
  if (msg.msg_type() == ActorMsgType::kCmdMsg) {
    CHECK_EQ(msg.actor_cmd(), ActorCmd::kEORD);
    OF_SET_MSG_HANDLE(&MdDiffAccActor::HandleWaitUntilNoReadableRegst);
  } else if (msg.msg_type() == ActorMsgType::kRegstMsg) {
    if (TryUpdtStateAsProducedRegst(msg.regst_wrapper()->regst_raw_ptr())
        != 0) {
      waiting_in_regst_.push(msg.regst_wrapper());
    }
  }
  ActUntilFail();
  return 0;
}

int MdDiffAccActor::HandleWaitUntilNoReadableRegst(const ActorMsg& msg) {
  CHECK_EQ(TryUpdtStateAsProducedRegst(msg.regst_wrapper()->regst_raw_ptr()),
           0);
  ActUntilFail();
  if (waiting_in_regst_.empty()) {
    AsyncSendEORDMsgForAllProducedRegstDesc();
    if (total_reading_cnt() == 0) {
      OF_SET_MSG_HANDLE(nullptr);
      return 1;
    } else {
      OF_SET_MSG_HANDLE(&MdDiffAccActor::HandleWaitUntilReadingCntEqualZero);
      return 0;
    }
  }
  return 0;
}

void MdDiffAccActor::Act() {
  std::shared_ptr<RegstWrapper> regst_wp = waiting_in_regst_.front();
  CHECK_EQ(regst_wp->piece_id(), expected_piece_id());
  KernelCtx ctx = GenDefaultKernelCtx();
  ForEachCurWriteableRegst([&](Regst* regst) {
    auto diff_cnt = model_diff_acc_cnt_.find(regst);
    if (diff_cnt->second != JobDesc::Singleton()->num_of_piece_in_batch()) {
      return;
    }
    Blob* packed_blob = regst->GetBlobPtrFromLbn(kPackedBlobName);
    MemsetFunc(ctx, packed_blob->mut_dptr(), 0,
               packed_blob->shape().elem_cnt()
                   * JobDesc::Singleton()->FloatingPointSize());
    diff_cnt->second = 0;
  });
  AsyncLaunchKernel(
      ctx, [this](uint64_t regst_desc_id) -> std::shared_ptr<RegstWrapper> {
        Regst* regst = GetCurWriteableRegst(regst_desc_id);
        if (regst == nullptr) {
          CHECK_EQ(regst_desc_id, waiting_in_regst_.front()->regst_desc_id());
          return waiting_in_regst_.front();
        } else {
          return std::make_shared<LocalRegstWrapper>(regst);
        }
      });
  AsyncSendReadableRegstMsg([this, &regst_wp](Regst* regst) {
    regst->set_piece_id(regst_wp->piece_id());
    ++model_diff_acc_cnt_.at(regst);
  });
  AsyncSendRegstMsgToProducer(regst_wp);
  waiting_in_regst_.pop();
}

}  // namespace oneflow
