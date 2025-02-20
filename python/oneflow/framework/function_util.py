"""
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
"""
import copy
import functools
import inspect
import re
import sys
import traceback
from typing import Any, Callable, Optional, Union

import oneflow._oneflow_internal
import oneflow.core.common.data_type_pb2 as data_type_pb
import oneflow.framework.session_context as session_ctx
import oneflow.support.enable_if as enable_if
import oneflow.support.pb_util as pb_util
from oneflow import oneflow_deprecate
from oneflow.framework.function_desc import FunctionDesc


class FunctionConfig(object):
    """OneFlow function's configurations.
    """

    def __init__(self) -> None:
        self.function_desc = FunctionDesc()

    def __getattr__(
        self, attr_name: str
    ) -> Callable[[Optional[Union[bool, int, float, str]]], None]:
        name2default = session_ctx.GetDefaultSession().function_flag_name2default_val
        assert attr_name in name2default
        flag_name2flag_value = self.function_desc.job_config_proto.flag_name2flag_value
        default_val = name2default[attr_name]

        def FunctionConfigSetter(
            attr_value: Optional[Union[bool, int, float, str]] = None
        ) -> None:
            if default_val.HasField("at_bool"):
                if attr_value is None:
                    attr_value = True
                assert type(attr_value) is bool
                flag_name2flag_value[attr_name].at_bool = attr_value
            elif default_val.HasField("at_int64"):
                assert type(attr_value) is int
                flag_name2flag_value[attr_name].at_int64 = attr_value
            elif default_val.HasField("at_double"):
                assert type(attr_value) is float
                flag_name2flag_value[attr_name].at_double = attr_value
            elif default_val.HasField("at_string"):
                assert type(attr_value) is str
                flag_name2flag_value[attr_name].at_string = attr_value
            else:
                raise NotImplementedError(
                    "config_flag `%s' with type %s is not supported"
                    % (attr_name, type(attr_value))
                )

        return FunctionConfigSetter


def _CloneFunctionDesc(func_desc, job_func):
    new_func_desc = FunctionDesc(job_func=job_func)
    new_func_desc.job_config_proto.CopyFrom(func_desc.job_config_proto)
    new_func_desc.function_attribute = copy.deepcopy(func_desc.function_attribute)
    return new_func_desc


def oneflow_function_config(*field_paths):
    def Decorator(func):
        global _class_property2return_obj_class
        for field_path in field_paths:
            fields = field_path.split(".")
            assert len(fields) > 0
            cls = FunctionConfig
            for (index, field) in enumerate(fields):
                assert field != "function_desc"
                assert re.match("^[_\\w]+[_\\w\\d]*$", field)
                if (cls, field) not in _class_property2return_obj_class:
                    class_name = ".".join(["function_config"] + fields[: index + 1])

                    def Init(self, function_desc):
                        self.function_desc = function_desc

                    config_class = type(class_name, (object,), dict(__init__=Init))
                    setattr(cls, field, _MakeInnerJobConfigClassProperty(config_class))
                    _class_property2return_obj_class[cls, field] = config_class
                cls = _class_property2return_obj_class[cls, field]
            cls.__call__ = _MakeLeafJobConfigCall(func)
        return func

    return Decorator


_class_property2return_obj_class = {}


def _MakeInnerJobConfigClassProperty(return_obj_class):
    return property(lambda self: return_obj_class(self.function_desc))


def _MakeLeafJobConfigCall(method):
    return lambda self, *argv, **kwarg: method(self.function_desc, *argv, **kwarg)


@oneflow_function_config("default_data_type")
def set_default_data_type(func_desc, value):
    """Set default data type for job

    Args:
        func_desc ([type]): job function
        value ([type]): data type. e.g. flow.float
    """
    func_desc.job_config_proto.default_data_type = oneflow._oneflow_internal.deprecated.GetProtoDtype4OfDtype(
        value
    )


@oneflow_function_config("default_initializer_conf")
def set_default_initializer_conf(func_desc, value):
    """Set default initial configuration for job

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    assert type(value) is dict
    pb_util.PythonDict2PbMessage(
        value, func_desc.job_config_proto.default_initializer_conf
    )


@oneflow_function_config("exp_run_conf")
def set_exp_run_conf(value):
    """Set experimental configuration for job

    Args:
        value ([type]): [description]
    """
    assert type(func_desc, value) is dict
    pb_util.PythonDict2PbMessage(value, func_desc.job_config_proto.exp_run_conf)


@oneflow_function_config("static_mem_alloc_policy_white_list.has")
def static_mem_alloc_policy_white_list_has_policy(func_desc, policy):
    """Get items from white list related to static memory allocation policy

    Args:
        func_desc ([type]): [description]
        policy ([type]): [description]

    Returns:
        [type]: [description]
    """
    return getattr(func_desc.job_config_proto.memory_allocation_algorithm_conf, policy)


@oneflow_function_config("static_mem_alloc_policy_white_list.add")
def static_mem_alloc_policy_white_list_add_policy(func_desc, policy):
    """Add item to white list related to static memory allocation policy

    Args:
        func_desc ([type]): [description]
        policy ([type]): [description]
    """
    setattr(func_desc.job_config_proto.memory_allocation_algorithm_conf, policy, True)


@oneflow_function_config("static_mem_alloc_policy_white_list.remove")
def static_mem_alloc_policy_white_list_remove_policy(func_desc, policy):
    """Remove item of white list related to static memory allocation policy

    Args:
        func_desc ([type]): [description]
        policy ([type]): [description]
    """
    setattr(func_desc.job_config_proto.memory_allocation_algorithm_conf, policy, False)


@oneflow_function_config("static_mem_alloc_policy_white_list.policy_mem_size_first")
def policy_mem_size_first(func_desc):
    """A static memory allocation policy called: mem_size_first

    Args:
        func_desc ([type]): [description]

    Returns:
        [type]: [description]
    """
    return "use_mem_size_first_algo"


@oneflow_function_config(
    "static_mem_alloc_policy_white_list.policy_mutual_exclusion_first"
)
def policy_mutual_exclusion_first(func_desc):
    """A static memory allocation policy called: mutual_exclusion_first

    Args:
        func_desc ([type]): [description]

    Returns:
        [type]: [description]
    """
    return "use_mutual_exclusion_first_algo"


@oneflow_function_config("static_mem_alloc_policy_white_list.policy_time_line")
def policy_time_line(func_desc):
    """A static memory allocation policy called: time_line

    Args:
        func_desc ([type]): [description]

    Returns:
        [type]: [description]
    """
    return "use_time_line_algo"


@oneflow_function_config("static_mem_alloc_algo_white_list.show")
def show_static_mem_alloc_algo_white_list(func_desc):
    """Show configuration of  static memory allocation policy,
          including: "use_mem_size_first_algo", "use_mutual_exclusion_first_algo", "use_time_line_algo"

    Args:
        func_desc ([type]): [description]

    Returns:
        [type]: [description]
    """
    return [
        "use_mem_size_first_algo",
        "use_mutual_exclusion_first_algo",
        "use_time_line_algo",
    ]


@oneflow_function_config("enable_cudnn")
def set_enable_cudnn(func_desc, value=True):
    """Whether use cudnn to accelerate job or not.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.enable_cudnn = value


@oneflow_function_config("cudnn_buf_limit_mbyte")
def set_cudnn_buf_limit_mbyte(func_desc, value):
    """Set cudnn buffer limit, e.g. 1024mb

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_buf_limit_mbyte = value


@oneflow_function_config("cudnn_conv_force_fwd_algo")
def set_cudnn_conv_force_fwd_algo(func_desc, value):
    """Set value to cudnn conv_force_forward algorithm

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_force_fwd_algo = value


@oneflow_function_config("cudnn_conv_force_bwd_data_algo")
def set_cudnn_conv_force_bwd_data_algo(func_desc, value):
    """Set value to cudnn conv_force_backward_data algorithm

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_force_bwd_data_algo = value


@oneflow_function_config("cudnn_conv_force_bwd_filter_algo")
def set_cudnn_conv_force_bwd_filter_algo(func_desc, value):
    """Set value to cudnn conv_force_backward_filter algorithm

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_force_bwd_filter_algo = value


@oneflow_function_config("cudnn_conv_heuristic_search_algo")
def set_cudnn_conv_heuristic_search_algo(func_desc, value):
    """Set value to cudnn conv_heuristic_search algorithm

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_heuristic_search_algo = value


@oneflow_function_config("enable_cudnn_fused_normalization_add_relu")
def set_enable_cudnn_fused_normalization_add_relu(func_desc, value):
    """Whether enable cudnn_fused_normalization_add_relu.

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.enable_cudnn_fused_normalization_add_relu = value


@oneflow_function_config("enable_fuse_add_to_output")
def set_enable_fuse_add_to_output(func_desc, value):
    """Whether enable fuse_add_to_output.
            If enabled, try to fuse a binary element-wise add to one of the predecessors to improve performance.

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.enable_fuse_add_to_output = value


@oneflow_function_config("enable_fuse_cast_scale")
def set_enable_fuse_cast_scale(func_desc, value=True):
    """Whether enable fuse_cast_scale.
            If enabled, try to fuse cast and scalar_mul_by_tensor to improve performance.

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.enable_fuse_cast_scale = value


@oneflow_function_config("cudnn_conv_use_deterministic_algo_only")
def set_cudnn_conv_use_deterministic_algo_only(func_desc, value):
    """Set value to cudnn conv_use_deterministic_only algorithm

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_use_deterministic_algo_only = value


@oneflow_function_config("enable_reuse_mem")
def set_enable_reused_mem(func_desc, value=True):
    """Whether enable reuse memory or not

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.enable_reuse_mem = value


@oneflow_function_config("enable_inplace")
def set_enable_inplace(func_desc, value=True):
    """Whether enable inplace  or not

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.enable_inplace = value


@oneflow_function_config("enable_inplace_in_reduce_struct")
def set_enable_inplace_in_reduce_struct(func_desc, value=True):
    print(
        "'enable_inplace_in_reduce_struct' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("enable_nccl")
def set_enable_nccl(func_desc, value=True):
    print(
        "'enable_nccl' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("use_nccl_inter_node_communication")
def set_use_nccl_inter_node_communication(func_desc, value=True):
    print(
        "'use_nccl_inter_node_communication' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("use_boxing_v2")
def set_use_boxing_v2(func_desc, value=True):
    print(
        "'use_boxing_v2' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("do_parallel_cast_before_widening_type_cast")
def set_do_parallel_cast_before_widening_type_cast(func_desc, value=True):
    func_desc.job_config_proto.do_parallel_cast_before_widening_type_cast = value


@oneflow_function_config("enable_all_reduce_group")
def set_enable_all_reduce_group(func_desc, value=True):
    print(
        "'enable_all_reduce_group' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("all_reduce_group_num")
def set_all_reduce_group_num(func_desc, value):
    print(
        "'all_reduce_group_num' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("all_reduce_lazy_ratio")
def set_all_reduce_lazy_ratio(func_desc, value):
    print(
        "'all_reduce_lazy_ratio' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("all_reduce_group_min_mbyte")
def set_all_reduce_group_min_mbyte(func_desc, value):
    print(
        "'all_reduce_group_min_mbyte' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("all_reduce_group_size_warmup")
def set_all_reduce_group_size_warmup(func_desc, value):
    print(
        "'all_reduce_group_size_warmup' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("all_reduce_fp16")
def set_all_reduce_fp16(func_desc, value=True):
    print(
        "'all_reduce_fp16' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config(
    "optimizer_placement_optimization_mode",
    "train.optimizer_placement_optimization_mode",
)
def set_optimizer_placement_optimization_mode(func_desc, mode):
    """Enable optimizer_placement_optimization with mode 'mode'

    Args:
        func_desc ([type]): [description]
        mode (str): [description].
    """
    assert mode in ["non_distributed", "distributed_split"]
    func_desc.job_config_proto.optimizer_placement_optimization_mode = mode


@oneflow_function_config(
    "optimizer_placement_optimization_threshold",
    "train.optimizer_placement_optimization_threshold",
)
def set_optimizer_placement_optimization_threshold(func_desc, value):
    func_desc.job_config_proto.optimizer_placement_optimization_threshold = value


@oneflow_function_config("enable_non_distributed_optimizer")
def set_enable_non_distributed_optimizer(func_desc, value=True):
    """Whether enable non_distributed optimizer or not

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    if value:
        set_optimizer_placement_optimization_mode(func_desc, "non_distributed")


@oneflow_function_config("disable_all_reduce_sequence")
def set_disable_all_reduce_sequence(func_desc, value=True):
    print(
        "'disable_all_reduce_sequence' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config("prune_parallel_cast_ops")
def set_prune_parallel_cast_ops(func_desc, value=True):
    """Whether prune parallel cast  operations or not.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.prune_parallel_cast_ops = value


@oneflow_function_config("prune_cast_to_static_shape_ops")
def set_prune_cast_to_static_shape_ops(func_desc, value=True):
    """Whether or not set prune_cast to static shape opretions

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.prune_cast_to_static_shape_ops = value


@oneflow_function_config("prune_amp_white_identity_ops")
def set_prune_amp_white_identity_ops(func_desc, value=True):
    """Whether prune amp_white_identity operations or not.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.prune_amp_white_identity_ops = value


@oneflow_function_config("non_distributed_optimizer_group_size_mbyte")
def set_non_distributed_optimizer_group_size_mbyte(func_desc, value):
    print(
        "'non_distributed_optimizer_group_size_mbyte' has been deprecated, has no effect and will be removed in the future."
    )


@oneflow_function_config(
    "enable_true_half_config_when_conv", "cudnn_conv_enable_true_half"
)
def set_cudnn_conv_enable_true_half(func_desc, value=True):
    """Whether  use true_half mode or not during  convolution calculation process while using cudnn.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.cudnn_conv_enable_pseudo_half = not value


@oneflow_function_config(
    "cudnn_conv_enable_pseudo_half", "enable_cudnn_conv_pseudo_half"
)
def set_cudnn_conv_enable_pseudo_half(func_desc, value):
    """Whether  enable pseudo_half mode or not during  convolution calculation process while using cudnn

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.cudnn_conv_enable_pseudo_half = value


@oneflow_function_config("enable_float_compute_for_half_gemm")
def set_enable_float_compute_for_half_gemm(func_desc, value=True):
    """Whether  enable float_compute or not ,
          if True, means that the type of intermedia value is float when compute half gemm.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    print(
        "WARNING: enable_float_compute_for_half_gemm has been deprecated, because we always use float compute for half gemm. Please remove it.\n        "
    )
    print(traceback.format_stack()[-3])


@oneflow_function_config("enable_quantization_aware_training")
@oneflow_function_config("enable_qat")
def set_enable_quantization_aware_training(func_desc, value=True):
    """If true, then job will use quantization aware training

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.enable_quantization_aware_training = value


@oneflow_function_config("qat.per_channel_weight_quantization")
def set_qat_per_channel(func_desc, value=True):
    func_desc.job_config_proto.qat_config.per_channel_weight_quantization = value


@oneflow_function_config("qat.symmetric")
def set_qat_symmetric(func_desc, value=True):
    func_desc.job_config_proto.qat_config.symmetric = value


@oneflow_function_config("qat.moving_min_max_momentum")
def set_qat_moving_min_max_momentum(func_desc, value: float):
    func_desc.job_config_proto.qat_config.moving_min_max_momentum = value


@oneflow_function_config("qat.moving_min_max_stop_update_after_iters")
def set_qat_moving_min_max_momentum(func_desc, value: float):
    func_desc.job_config_proto.qat_config.moving_min_max_stop_update_after_iters = value


@oneflow_function_config("qat.target_backend")
def set_qat_symmetric(func_desc, value: str):
    func_desc.job_config_proto.qat_config.target_backend = value


@oneflow_function_config("enable_auto_mixed_precision")
def set_enable_auto_mixed_precision(func_desc, value=True):
    """If true, then job will use mixed precision mode, it means use both float16 and float32 during model training.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    func_desc.job_config_proto.enable_auto_mixed_precision = value


@oneflow_function_config("enable_keep_header_only")
def set_enable_keep_header_only(func_desc, value=True):
    """deprecated api.

    Args:
        func_desc ([type]): [description]
        value (bool, optional): [description]. Defaults to True.
    """
    print("Sorry! enable_keep_header_only is deprecated and it doesn't work.\n")


@oneflow_function_config("concurrency_width")
def set_concurrency_width(func_desc, value):
    """Set up concurrency width

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.concurrency_width = value


@oneflow_function_config("train.model_update_conf")
def set_model_update_conf(func_desc, value):
    """Set up optimizer and update method of learning rate  for job

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    print(
        "WARNING: func_config.train.* has been deprecated. Please replace it by the new optimizer api.\n        "
    )
    print(traceback.format_stack()[-3])
    assert type(value) is dict
    func_desc.job_config_proto.train_conf.model_update_conf.SetInParent()
    pb_util.PythonDict2PbMessage(
        value, func_desc.job_config_proto.train_conf.model_update_conf
    )


@oneflow_function_config("indexed_slices_optimizer_conf")
def set_indexed_slices_optimizer_conf(func_desc, value):
    """Set indexed slices configuration of optimizer

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    assert type(value) is dict
    func_desc.job_config_proto.indexed_slices_optimizer_conf.SetInParent()
    pb_util.PythonDict2PbMessage(
        value, func_desc.job_config_proto.indexed_slices_optimizer_conf
    )


@oneflow_function_config("enable_fuse_model_update_ops")
def set_enable_fuse_model_update_ops(func_desc, value=True):
    """Whether enable fuse_model_update_ops.
            If enabled, try to fuse cast + scale + l1_l2_regularize_gradient + model_update to one op to improve performance.

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.enable_fuse_model_update_ops = value


@oneflow_function_config("enable_gradients_stats_aggregation")
def set_enable_gradients_stats_aggregation(func_desc, value=True):
    """Whether enable gradients_stats_aggregation.
            If enabled, gradients stats ops (norm, finite, ...) will be aggregated.

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.job_config_proto.enable_gradients_stats_aggregation = value


@oneflow_function_config("train.loss_scale_factor")
def set_loss_scale_factor(func_desc, value):
    """Set scale factor for loss

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    print(
        "WARNING: func_config.train.* has been deprecated. Please replace it by the new optimizer api.\n        "
    )
    print(traceback.format_stack()[-3])
    func_desc.job_config_proto.train_conf.loss_scale_factor = value


@oneflow_function_config("train.primary_lr")
def set_primary_lr(func_desc, value):
    """Set the primary leaning rate for job

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    print(
        "WARNING: func_config.train.* has been deprecated. Please replace it by the new optimizer api.\n        "
    )
    print(traceback.format_stack()[-3])
    func_desc.job_config_proto.train_conf.primary_lr = value


@oneflow_function_config("train.secondary_lr")
def set_secondary_lr(func_desc, value):
    """Set the secondary leaning rate for job

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    print(
        "WARNING: func_config.train.* has been deprecated. Please replace it by the new optimizer api.\n        "
    )
    print(traceback.format_stack()[-3])
    func_desc.job_config_proto.train_conf.secondary_lr = value


@oneflow_function_config("train.num_gradient_accumulation_steps")
def set_num_gradient_accumulation_steps(func_desc, value):
    func_desc.job_config_proto.num_gradient_accumulation_steps = value


@oneflow_function_config("default_logical_view")
def set_default_distribute_strategy(func_desc, value):
    """Set up default distribute strategy for job

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    assert isinstance(value, distribute_ctx.DistributeStrategy)
    func_desc.function_attribute.default_distribute_strategy = value


@oneflow_function_config("allow_cpu_return_op")
def allow_cpu_return_op(func_desc, value):
    """Whether allow operaions returned from cpu or  not

    Args:
        func_desc ([type]): [description]
        value ([type]): [description]
    """
    func_desc.function_attribute.allow_cpu_return_op = value


@oneflow_function_config("default_distribute_strategy")
@oneflow_deprecate()
def deprecated_set_default_distribute_strategy(*args, **kwargs):
    print(
        "WARNING:",
        "function_config.default_distribute_strategy",
        "has been deprecated. Please use {} instead.".format(
            "function_config.default_logical_view"
        ),
    )
    print(traceback.format_stack()[-3], file=sys.stderr)
    set_default_distribute_strategy(*args, **kwargs)
