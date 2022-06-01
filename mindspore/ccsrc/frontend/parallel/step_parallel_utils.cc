/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "frontend/parallel/step_parallel_utils.h"

#include <inttypes.h>
#include <sys/time.h>
#include <algorithm>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <queue>
#include <memory>

#include "utils/hash_map.h"
#include "mindspore/core/ops/core_ops.h"
#include "frontend/operator/ops.h"
#include "frontend/optimizer/optimizer.h"
#include "include/common/utils/parallel_context.h"
#include "frontend/parallel/device_manager.h"
#include "frontend/parallel/graph_util/generate_graph.h"
#include "frontend/parallel/graph_util/graph_info.h"
#include "frontend/parallel/graph_util/node_info.h"
#include "frontend/parallel/graph_util/pipeline_split_utils.h"
#include "frontend/parallel/node_check.h"
#include "frontend/parallel/parameter_manager.h"
#include "ir/param_info.h"
#include "ir/tensor.h"
#include "utils/trace_base.h"
#include "include/common/utils/comm_manager.h"
#include "utils/ms_context.h"
#include "utils/symbolic.h"
#include "mindspore/core/utils/parallel_node_check.h"

namespace mindspore {
namespace parallel {
bool IsSomePrimitive(const CNodePtr &cnode, const std::string &name) {
  if (!cnode) return false;
  ValueNodePtr anf_node = cnode->input(0)->cast<ValueNodePtr>();
  if (!anf_node) return false;
  PrimitivePtr prim = anf_node->value()->cast<PrimitivePtr>();
  if (!prim) return false;
  return (prim->name() == name);
}

bool IsSomePrimitiveList(const CNodePtr &cnode, const std::set<string> &check_list) {
  return std::any_of(check_list.begin(), check_list.end(),
                     [cnode](const string &in) { return IsSomePrimitive(cnode, in); });
}

std::string GetPrimName(const CNodePtr &node) {
  auto prim = GetCNodePrimitive(node);
  MS_EXCEPTION_IF_NULL(prim);
  return prim->name();
}

TensorInfo GetInputsTensorInfo(const std::pair<AnfNodePtr, int64_t> &param_info) {
  auto user_cnode = param_info.first->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(user_cnode);
  auto user_input_index = param_info.second;
  OperatorInfoPtr op_info = user_cnode->user_data<OperatorInfo>();
  MS_EXCEPTION_IF_NULL(op_info);

  TensorInfo tensor_info;
  if (IsPrimitiveCNode(user_cnode, prim::kPrimSend)) {
    auto param_index = IntToSize(GetValue<int>(user_cnode->GetPrimalAttr(PARAM_INDEX)));
    tensor_info = op_info->inputs_tensor_info()[param_index];
  } else {
    size_t input_tensor_info_size = op_info->inputs_tensor_info().size();
    if (SizeToLong(input_tensor_info_size) <= user_input_index - 1) {
      MS_LOG(EXCEPTION) << op_info->name() << ": the size of inputs tensor info is " << input_tensor_info_size
                        << ", but the index is " << (user_input_index - 1);
    }
    tensor_info = op_info->inputs_tensor_info()[LongToSize(user_input_index - 1)];
  }
  return tensor_info;
}

AnfNodePtr GetRealKernelNode(const AnfNodePtr &node, int64_t get_item_index, CNodePtr *call_node) {
  if (IsPrimitiveCNode(node, prim::kPrimDepend) || IsPrimitiveCNode(node, prim::kPrimLoad) ||
      IsPrimitiveCNode(node, prim::kPrimCast)) {
    return GetRealKernelNode(node->cast<CNodePtr>()->input(1), get_item_index, call_node);
  }
  if (IsPrimitiveCNode(node, prim::kPrimTupleGetItem)) {
    auto cnode = node->cast<CNodePtr>();
    auto cur_get_item_index = LongToInt(GetTupleGetItemIndex(cnode));
    auto tuple_getitem_input = cnode->input(1);
    auto pass_through_node = GetRealKernelNode(tuple_getitem_input, cur_get_item_index, call_node);
    return GetRealKernelNode(pass_through_node, get_item_index, call_node);
  }
  if (get_item_index != -1 && IsPrimitiveCNode(node, prim::kPrimMakeTuple)) {
    auto make_tuple_cnode = node->cast<CNodePtr>();
    auto make_tuple_input = make_tuple_cnode->input(LongToSize(get_item_index + 1));
    return GetRealKernelNode(make_tuple_input, -1, call_node);
  }
  if (node->isa<CNode>() && IsValueNode<FuncGraph>(node->cast<CNodePtr>()->input(0))) {
    if (call_node != nullptr && *call_node == nullptr) {
      *call_node = node->cast<CNodePtr>();
    }
    auto cnode = node->cast<CNodePtr>();
    auto graph = GetValueNode<FuncGraphPtr>(cnode->input(0));
    auto output = GetRealKernelNode(graph->output(), get_item_index, call_node);
    MS_EXCEPTION_IF_NULL(output);
    if (output->isa<Parameter>()) {
      auto parameters = graph->parameters();
      auto pos_iter = std::find(parameters.begin(), parameters.end(), output);
      // If can't find in parameters, the parameter is a fv.
      if (pos_iter == parameters.end()) {
        return output;
      }
      auto pos = std::distance(parameters.begin(), pos_iter);
      return GetRealKernelNode(cnode->input(LongToSize(pos + 1)), -1, call_node);
    }
    return output;
  }
  return node;
}

AnfNodePtr CheckMakeTupleSplit(const AnfNodePtr &node, const FuncGraphManagerPtr &manager) {
  auto node_users = manager->node_users()[node];

  bool is_first_tensor_info = true;
  TensorInfo first_tensor_info;
  AnfNodePtr first_node;
  for (auto &node_user : node_users) {
    auto user_node = node_user.first->cast<CNodePtr>();
    if (!user_node->has_user_data<OperatorInfo>()) {
      continue;
    }
    auto tensor_info = GetInputsTensorInfo(node_user);
    if (is_first_tensor_info) {
      is_first_tensor_info = false;
      first_tensor_info = tensor_info;
      first_node = node_user.first;
      continue;
    }
    if (first_tensor_info == tensor_info) {
      continue;
    } else {
      MS_LOG(EXCEPTION) << "The node: " << node->DebugString()
                        << " has multiple users, but the TensorInfo are different";
    }
  }
  return first_node;
}

bool IsParallelCareNode(const CNodePtr &cnode) {
  MS_EXCEPTION_IF_NULL(cnode);
  ValueNodePtr prim_node = cnode->input(0)->cast<ValueNodePtr>();
  if (prim_node == nullptr) {
    return false;
  }
  PrimitivePtr prim = prim_node->value()->cast<PrimitivePtr>();
  if (prim == nullptr) {
    return false;
  }
  if (IsInParallelBlackList(prim)) {
    MS_LOG(DEBUG) << "Parallel don't care node: " << prim->name();
    return false;
  }
  // get_next is not in the forward graph, we need mark the get_next as the forward node
  if (prim->name() == GET_NEXT || prim->name() == VIRTUAL_OUTPUT) {
    return true;
  }
  if ((prim->name() == CAST) && !cnode->has_user_data<OperatorInfo>()) {
    return false;
  }

  return cnode->in_forward_flag();
}

Shapes GetValueListShape(const AnfNodePtr &node) {
  Shapes shapes;
  std::vector<ValuePtr> inputs_seq;
  if (IsValueNode<ValueList>(node)) {
    inputs_seq = node->cast<ValueNodePtr>()->value()->cast<ValueListPtr>()->value();
  } else if (IsValueNode<ValueTuple>(node)) {
    inputs_seq = node->cast<ValueNodePtr>()->value()->cast<ValueTuplePtr>()->value();
  } else {
    MS_LOG(EXCEPTION) << "node is eigther ValueList or ValueTuple";
  }
  for (auto &ele : inputs_seq) {
    auto tensor = ele->cast<tensor::TensorPtr>();
    if (tensor == nullptr) {
      MS_LOG(WARNING) << "The value node is not a tensor";
      break;
    }
    auto one_shape = tensor->shape();
    shapes.push_back(one_shape);
  }
  return shapes;
}

bool IsControlFlowNode(const AnfNodePtr &node) {
  // Only switch or FuncCall nodes are control flow nodes
  MS_EXCEPTION_IF_NULL(node);
  if (!node->isa<CNode>()) {
    return false;
  }
  auto cnode = node->cast<CNodePtr>();
  MS_EXCEPTION_IF_NULL(cnode);
  // func node
  if (cnode->input(0)->isa<CNode>() && IsPrimitiveCNode(cnode->input(0), prim::kPrimSwitch)) {
    return true;
  }
  return false;
}

int64_t GetTupleGetItemIndex(const CNodePtr &cnode) {
  MS_EXCEPTION_IF_NULL(cnode);
  if (!cnode->input(TUPLE_GETITEM_INDEX_POS)->isa<ValueNode>()) {
    MS_LOG(EXCEPTION) << "The index of tuple getitem is not a value node";
  }

  ValuePtr tuple_index_value = GetValueNode(cnode->input(TUPLE_GETITEM_INDEX_POS));
  MS_EXCEPTION_IF_NULL(tuple_index_value);
  if (!tuple_index_value->isa<Int64Imm>()) {
    MS_LOG(EXCEPTION) << "The index of tuple getitem is not int64";
  }
  return tuple_index_value->cast<Int64ImmPtr>()->value();
}

void RedistributionNextNode(const AnfNodePtr &node, const FuncGraphManagerPtr &manager, NodeUsersMap *node_users_map,
                            int64_t get_item_index,
                            std::vector<std::pair<std::pair<AnfNodePtr, int>, int>> *next_nodes) {
  MS_EXCEPTION_IF_NULL(node);
  MS_EXCEPTION_IF_NULL(node_users_map);
  auto node_set = (*node_users_map)[node];
  for (auto &node_pair : node_set) {
    auto use_cnode = node_pair.first->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(use_cnode);
    if (IsValueNode<FuncGraph>(use_cnode->input(0))) {
      auto fg = GetValueNode<FuncGraphPtr>(use_cnode->input(0));
      MS_EXCEPTION_IF_NULL(fg);
      auto fg_parameters = fg->parameters();
      auto param = fg_parameters[node_pair.second - 1];
      MS_EXCEPTION_IF_NULL(param);
      RedistributionNextNode(param, manager, node_users_map, get_item_index, next_nodes);
      continue;
    }
    if (IsPrimitiveCNode(use_cnode, prim::kPrimTupleGetItem)) {
      get_item_index = LongToInt(GetTupleGetItemIndex(use_cnode));
    }
    // depend, auto monad and control flow op don't need to jump over
    if ((IsPrimitiveCNode(use_cnode, prim::kPrimDepend) && node_pair.second != 1) ||
        IsPrimitiveCNode(use_cnode, prim::kPrimUpdateState) || IsPrimitiveCNode(use_cnode, prim::kPrimSwitch)) {
      continue;
    }
    if (IsParallelCareNode(use_cnode) && use_cnode->has_user_data<OperatorInfo>()) {
      next_nodes->push_back(std::make_pair(node_pair, get_item_index));
    } else {
      // search recursively
      RedistributionNextNode(use_cnode, manager, node_users_map, get_item_index, next_nodes);
    }
  }
}

void RedistributionPreNode(const CNodePtr &cnode, const FuncGraphManagerPtr &manager,
                           std::vector<AnfNodePtr> *pre_nodes) {
  if (IsValueNode<FuncGraph>(cnode->input(0))) {
    auto fg = GetValueNode<FuncGraphPtr>(cnode->input(0));
    auto pre_node = GetRealKernelNode(fg->output(), -1, nullptr);
    if (!pre_node) {
      return;
    }
    auto pre_cnode = pre_node->cast<CNodePtr>();
    if (!pre_cnode) {
      return;
    }
    if (IsParallelCareNode(pre_cnode) && pre_cnode->has_user_data<OperatorInfo>()) {
      pre_nodes->push_back(pre_cnode);
    } else {
      RedistributionPreNode(pre_cnode, pre_cnode->func_graph()->manager(), pre_nodes);
    }
  }
  if (IsControlFlowNode(cnode)) {
    auto switch_cnode = cnode->input(0)->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(switch_cnode);
    // extract true branch, false branch is usually also a control flow graph
    auto fg = GetValueNode<FuncGraphPtr>(switch_cnode->input(2));
    MS_EXCEPTION_IF_NULL(fg);
    auto fg_out = fg->output()->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(fg_out);
    // control flow node, need enter graph to find redistribution pre node.
    RedistributionPreNode(fg_out, manager, pre_nodes);
  }
  if (IsPrimitiveCNode(cnode, prim::kPrimDepend) || IsPrimitiveCNode(cnode, prim::kPrimLoad) ||
      IsPrimitiveCNode(cnode, prim::kPrimCast) || IsPrimitiveCNode(cnode, prim::kPrimAllReduce)) {
    auto cnode_input = cnode->input(1)->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode_input);
    RedistributionPreNode(cnode_input, manager, pre_nodes);
  }
  if (IsParallelCareNode(cnode) && cnode->has_user_data<OperatorInfo>()) {
    pre_nodes->push_back(cnode);
  }
}

Shapes GetNodeShape(const AnfNodePtr &node) {
  MS_EXCEPTION_IF_NULL(node);
  Shapes shapes;
  if (IsValueNode<ValueList>(node) || IsValueNode<ValueTuple>(node)) {
    return GetValueListShape(node);
  }
  BaseShapePtr base_shape_ptr = node->Shape();
  if (node->isa<CNode>() && !IsControlFlowNode(node)) {
    auto cnode = node->cast<CNodePtr>();
    if (cnode->input(0)->isa<CNode>()) {
      if (cnode->inputs().size() < 2) {
        MS_LOG(EXCEPTION) << "GetNodeShape: " << node->ToString() << " size is smaller than 2";
      }
      base_shape_ptr = cnode->input(1)->Shape();
    }
  }
  if (base_shape_ptr == nullptr) {
    MS_LOG(EXCEPTION) << "GetNodeShape: " << node->ToString() << " shape_ptr is nullptr, full name is "
                      << node->fullname_with_scope();
  }
  auto tuple_shape_ptr = dyn_cast<abstract::SequenceShape>(base_shape_ptr);
  if (tuple_shape_ptr != nullptr) {
    auto tuple_shape = tuple_shape_ptr->shape();
    for (auto &shape : tuple_shape) {
      auto each_shape = dyn_cast<abstract::Shape>(shape);
      MS_EXCEPTION_IF_NULL(each_shape);
      shapes.push_back(each_shape->shape());
    }
  } else {
    auto shape_ptr = dyn_cast<abstract::Shape>(base_shape_ptr);
    MS_EXCEPTION_IF_NULL(shape_ptr);
    shapes.push_back(shape_ptr->shape());
  }
  return shapes;
}

RankList FindCommonMirrorGroup(const FuncGraphPtr &root) {
  auto parameters = root->parameters();
  for (auto &parameter : parameters) {
    auto param_ptr = parameter->cast<ParameterPtr>();
    MS_EXCEPTION_IF_NULL(param_ptr);
    if (!(param_ptr->has_default() && ParameterRequireGrad(param_ptr))) {
      continue;
    }
    size_t allow_repeat_num = 1;
    if (ParallelContext::GetInstance()->enable_parallel_optimizer() &&
        (!param_ptr->param_info() || param_ptr->param_info()->parallel_optimizer())) {
      if (ParallelContext::GetInstance()->optimizer_weight_shard_size() == -1) {
        MS_LOG(WARNING) << "The parameter :" << param_ptr->fullname_with_scope()
                        << " is fully shard by optimizer parallel,"
                           " thus cannot find common data parallel group for this rank";
        return {g_device_manager->global_rank()};
      }
      allow_repeat_num = size_t(ParallelContext::GetInstance()->optimizer_weight_shard_size());
    }
    if (IsFullySplitParameter(param_ptr, allow_repeat_num)) {
      MS_LOG(WARNING) << "The parameter :" << param_ptr->fullname_with_scope()
                      << " is fully shard, thus cannot find common data parallel group for this rank";
      return {g_device_manager->global_rank()};
    }
  }
  AnfNodePtr ret = root->get_return();
  MS_EXCEPTION_IF_NULL(ret);
  std::vector<int64_t> common_group_list;
  std::vector<AnfNodePtr> all_nodes = DeepScopedGraphSearch(ret);
  bool is_first_group = true;
  for (auto &node : all_nodes) {
    if (!IsPrimitiveCNode(node, prim::kPrimMirror) && !IsPrimitiveCNode(node, prim::kPrimMirrorMicroStep) &&
        !IsPrimitiveCNode(node, prim::kPrimMirrorMiniStep)) {
      continue;
    }
    auto prim = GetCNodePrimitive(node);
    if (!prim->HasAttr(GROUP)) {
      MS_LOG(EXCEPTION) << "The mirror operator dose not have group attr : " << node->DebugString();
    }
    std::string group_name = GetValue<std::string>(prim->GetAttr(GROUP));
    std::vector<int64_t> group_list = g_device_manager->FindRankListByHashName(group_name);
    if (is_first_group) {
      common_group_list = group_list;
      is_first_group = false;
    } else {
      std::vector<int64_t> new_comm_group_list;
      (void)std::set_intersection(common_group_list.begin(), common_group_list.end(), group_list.begin(),
                                  group_list.end(), std::back_inserter(new_comm_group_list));
      common_group_list = new_comm_group_list;
    }
  }
  MS_LOG(INFO) << "The common mirror group is:" << common_group_list;
  return common_group_list;
}

std::string CreateInstanceName(const CNodePtr &node, size_t index) {
  MS_EXCEPTION_IF_NULL(node);
  if (!IsValueNode<Primitive>(node->input(0))) {
    MS_LOG(EXCEPTION) << "CreateInstanceName: " << node->ToString() << " doesn't have primitive";
  }
  std::string name_base = node->fullname_with_scope();
  std::string name = name_base + "_" + std::to_string(index);
  std::string instance_name = HashInstanceName(name);
  return instance_name;
}

void SetCommunicationOpGroupLabel(std::vector<AnfNodePtr> new_node_input) {
  if (new_node_input.empty()) {
    return;
  }

  auto prim_anf_node = new_node_input[0]->cast<ValueNodePtr>();
  auto prim = GetValueNode<PrimitivePtr>(prim_anf_node);
  MS_EXCEPTION_IF_NULL(prim);

  auto attrs = prim->attrs();
  auto iter = attrs.find(GROUP);
  if (iter != attrs.end()) {
    auto value = iter->second;
    MS_EXCEPTION_IF_NULL(value);
    if (value->isa<StringImm>()) {
      std::string hash_name = value->cast<StringImmPtr>()->value();
      MS_EXCEPTION_IF_NULL(g_device_manager);
      std::string rank_list_name = g_device_manager->FindRankListNameByHashName(hash_name);
      (void)prim->AddAttr(GROUP_RANKS, MakeValue(rank_list_name));
    }
  }
}

std::vector<AnfNodePtr> ReplaceOpInput(const Operator &replace_op, const std::string &instance_name,
                                       const CNodePtr &node) {
  OperatorArgs arg_replace_op = replace_op.second;
  ValuePtr pyop_instance = CreateOpInstance(arg_replace_op.first, replace_op.first, instance_name);
  if (pyop_instance == nullptr) {
    MS_LOG(EXCEPTION) << "Failure: " << replace_op.first << " CreateOpInstance failed";
  }
  OperatorParams params = arg_replace_op.second;
  if (node->inputs().size() < 2) {
    // GetNext operator dose not has input
    if (node->inputs().size() == 1) {
      return {NewValueNode(pyop_instance)};
    }
    MS_LOG(EXCEPTION) << "Failure: " << node->ToString() << " size is smaller than 2";
  }
  std::vector<AnfNodePtr> replace_input = {NewValueNode(pyop_instance), node->input(1)};

  if (replace_op.first == EMBEDDING_LOOKUP) {
    replace_input = {NewValueNode(pyop_instance), node->input(1), node->input(2)};
  }

  if (!params.empty()) {
    Param param_first = *(params.begin());
    int64_t first_position = param_first.second;
    if (first_position == 1) {
      replace_input.pop_back();
    }
    for (auto &param : params) {
      AnfNodePtr val = NewValueNode(param.first.second);
      if (val == nullptr) {
        MS_LOG(EXCEPTION) << "Failure:val is nullptr";
      }
      int64_t position = param.second;
      (void)replace_input.insert(replace_input.begin() + position, val);
    }
  } else if (replace_op.first == SYNC_BATCH_NORM) {
    for (size_t i = 2; i < node->inputs().size(); ++i) {
      replace_input.push_back(node->input(i));
    }
  }
  SetCommunicationOpGroupLabel(replace_input);
  return replace_input;
}

void SetStridedSliceSplitStrategy(const std::vector<AnfNodePtr> &all_nodes) {
  for (auto &node : all_nodes) {
    if (!node->isa<CNode>()) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    MS_EXCEPTION_IF_NULL(cnode);
    if (!IsPrimitiveCNode(cnode, prim::kPrimStridedSlice)) {
      continue;
    }
    auto slice_prim = GetCNodePrimitive(cnode);
    MS_EXCEPTION_IF_NULL(slice_prim);
    if (slice_prim->HasAttr(FUNC_GRAPH_FLAG_STRIDED_SLICE)) {
      SetStridedSliceStrategy(cnode);
    }
  }
}

// Check the given tensor, return nullptr if the given type is not an TensorType
bool CheckTensorType(const TypePtr &node_type) {
  MS_EXCEPTION_IF_NULL(node_type);
  if (!node_type->isa<mindspore::TensorType>()) {
    return false;
  }
  return true;
}

// For the weight used by cast and matmul at the same time, like the followings
// weight1->mirror->cast1-> matmul1;
// weight1->add
// we will not insert the cast(FP32->FP16), as it will cause the input of the operator add to be changed to fp16.
AnfNodePtr GetChildCastNode(const AnfNodePtr &node_ptr, const NodeUsersMap &node_users_map) {
  std::queue<AnfNodePtr> visited;
  AnfNodePtr queue_node = nullptr;
  CNodePtr cnode = nullptr;
  AnfNodePtr node = nullptr;
  if (!node_ptr) {
    return nullptr;
  }
  auto users = node_users_map.at(node_ptr);
  for (auto &node_user : users) {
    cnode = node_user.first->cast<CNodePtr>();
    if (!cnode || !cnode->in_forward_flag()) {
      continue;
    }
    if (node_user.first) {
      visited.push(node_user.first);
    }
  }
  while (!visited.empty()) {
    queue_node = visited.front();
    visited.pop();
    cnode = queue_node->cast<CNodePtr>();
    // MAKE_TUPLE will not appear after the load in the forward graph
    if (IsSomePrimitive(cnode, MAKE_TUPLE)) {
      continue;
    } else if (IsInAllGatherNodeList(cnode) || IsSomePrimitiveList(cnode, {LOAD, RESHAPE})) {
      auto node_set = node_users_map.at(queue_node);
      for (auto &node_user : node_set) {
        visited.push(node_user.first);
      }
    } else if (!IsSomePrimitive(cnode, CAST)) {
      MS_LOG(INFO) << "The weight's users including the non cast node So "
                   << "will not insert cast for this parameter " << node_ptr->DebugString();
      return nullptr;
    } else if (!node) {
      node = queue_node;
    }
  }
  return node;
}
// Given the cnode ptr, find its users until we find the computation node, then return the type of the
// computation node. This function is used to find the target type for CreateFP16Cast. Only returns the target type if
// it is float16, and the source node is float32. If the situation is not matched, then return the nullptr.
TypePtr FindChildCastWithFP32ToFP16(const CNodePtr &cnode_ptr, const NodeUsersMap &node_users_map) {
  auto node_ptr = cnode_ptr->cast<AnfNodePtr>();
  if (!node_ptr) {
    return nullptr;
  }
  auto cnode_inputs = cnode_ptr->inputs();
  if (cnode_inputs.size() < TWO_INPUT_SIZE) {
    return nullptr;
  }
  // As we execute the function IsWeightValidUsed when we start to insert the mirror, so the second parameter
  // is always the parameter.
  auto weight = cnode_inputs[1];
  if (!weight->isa<Parameter>()) {
    return nullptr;
  }
  MS_LOG(INFO) << "Start to search the weight params:" << weight->DebugString();

  AnfNodePtr node = GetChildCastNode(weight, node_users_map);
  if (!node) {
    return nullptr;
  }
  // get the output dtype of the operator
  auto node_type = node->Type();
  if (!CheckTensorType(node_type)) {
    return nullptr;
  }
  auto input_element_type = node_type->cast<mindspore::TensorTypePtr>()->element();
  MS_EXCEPTION_IF_NULL(input_element_type);
  auto source_node_type = node_ptr->Type();
  if (!CheckTensorType(source_node_type)) {
    return nullptr;
  }
  auto source_element_type = source_node_type->cast<mindspore::TensorTypePtr>()->element();
  MS_EXCEPTION_IF_NULL(input_element_type);
  // We only add cast operation when the source is fp32 type, and the users is fp16 type.
  if (source_element_type->type_id() == kNumberTypeFloat32 && input_element_type->type_id() == kNumberTypeFloat16) {
    return input_element_type;
  }
  return nullptr;
}

// Create a cast node given the current node and the previous node. The target type of the the cast is from the
// compute_node_type.
// Return the new cast node with pre_node as the inputs.
AnfNodePtr CreateFP16Cast(const CNodePtr &node, const AnfNodePtr &pre_node, const TypePtr &compute_node_type) {
  const char kOpsFunctionModelName[] = "mindspore.ops.functional";
  static py::object cast_prim = python_adapter::GetPyFn(kOpsFunctionModelName, "cast");
  const auto &adapter = py::cast<PrimitivePyAdapterPtr>(cast_prim);
  MS_EXCEPTION_IF_NULL(adapter);
  MS_EXCEPTION_IF_NULL(compute_node_type);
  auto prim = adapter->attached_primitive();
  if (prim == nullptr) {
    prim = std::make_shared<PrimitivePy>(cast_prim, adapter);
  }
  // Insert cast.
  auto type_node = NewValueNode(compute_node_type);
  type_node->set_abstract(compute_node_type->ToAbstract());
  auto new_node = node->func_graph()->NewCNode({NewValueNode(prim), pre_node, type_node});
  new_node->set_abstract(node->abstract());
  new_node->set_in_forward_flag(true);
  return new_node;
}

void LabelGenMaskMicro(const FuncGraphPtr &root) {
  AnfNodePtr ret = root->get_return();
  MS_EXCEPTION_IF_NULL(ret);
  std::vector<AnfNodePtr> all_nodes = DeepScopedGraphSearch(ret);
  for (auto &node : all_nodes) {
    if (IsPrimitiveCNode(node, prim::kPrimDropoutDoMask)) {
      auto gen_mask_node = RealInputNode(node->cast<CNodePtr>(), 2);
      if (gen_mask_node->isa<CNode>()) {
        gen_mask_node->cast<CNodePtr>()->set_primal_attrs(node->cast<CNodePtr>()->primal_attrs());
      }
    }
  }
}

void SetCastForParamNotRecompute(const std::vector<AnfNodePtr> &all_nodes) {
  for (const auto &node : all_nodes) {
    if (!IsPrimitiveCNode(node, prim::kPrimCast)) {
      continue;
    }
    auto cnode = node->cast<CNodePtr>();
    auto cast_input = RealInputNode(cnode, 1);
    if (cast_input->isa<Parameter>()) {
      MS_LOG(INFO) << "Cast for parameter no needs recompute to avoid redundant trans_data operator";
      PrimitivePtr prim = GetValueNode<PrimitivePtr>(cnode->input(0)->cast<ValueNodePtr>());
      (void)prim->AddAttr("recompute", MakeValue(false));
    }
  }
}

std::shared_ptr<Value> GetAttrsFromAnfNode(const std::shared_ptr<AnfNode> &node, const string &key) {
  if (!node) return nullptr;
  auto cnode = node->cast<CNodePtr>();
  auto prim = GetCNodePrimitive(cnode);
  if (prim && prim->HasAttr(key)) {
    return prim->GetAttr(key);
  }
  return nullptr;
}
}  // namespace parallel
}  // namespace mindspore
