/**
 * Copyright 2020-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_SIGMOID_CROSS_ENTROPY_WITH_LOGITS_GRAD_GPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_SIGMOID_CROSS_ENTROPY_WITH_LOGITS_GRAD_GPU_KERNEL_H_

#include <vector>
#include "backend/kernel_compiler/gpu/gpu_kernel.h"
#include "backend/kernel_compiler/gpu/gpu_kernel_factory.h"
#include "backend/kernel_compiler/gpu/cuda_impl/sigmoid_cross_entropy_with_logits_grad_impl.cuh"

namespace mindspore {
namespace kernel {
template <typename T, typename S>
class SigmoidCrossEntropyWithLogitsGradGpuKernelMod : public NativeGpuKernelMod {
 public:
  SigmoidCrossEntropyWithLogitsGradGpuKernelMod()
      : logits_size_(0), labels_size_(0), outputs_size_(0), is_null_input_(false) {}
  ~SigmoidCrossEntropyWithLogitsGradGpuKernelMod() override = default;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override {
    if (is_null_input_) {
      return true;
    }
    T *logits_addr = GetDeviceAddress<T>(inputs, 0);
    S *labels_addr = GetDeviceAddress<S>(inputs, 1);
    T *dout_addr = GetDeviceAddress<T>(inputs, 2);
    T *outputs_addr = GetDeviceAddress<T>(outputs, 0);

    SigmoidCrossEntropyWithLogitsGrad(inputs[0]->size / sizeof(T), logits_addr, labels_addr, dout_addr, outputs_addr,
                                      reinterpret_cast<cudaStream_t>(stream_ptr));
    return true;
  }

  bool Init(const CNodePtr &kernel_node) override {
    auto kernel_name = AnfAlgo::GetCNodeName(kernel_node);
    size_t input_num = AnfAlgo::GetInputTensorNum(kernel_node);
    if (input_num != 3) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name << "', the number of inputs should be 3, but got " << input_num;
    }
    logits_size_ = sizeof(T);
    labels_size_ = sizeof(S);
    outputs_size_ = sizeof(T);

    auto logits_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 0);
    auto labels_shape = AnfAlgo::GetPrevNodeOutputInferShape(kernel_node, 1);
    auto output_shape = AnfAlgo::GetOutputInferShape(kernel_node, 0);
    is_null_input_ = CHECK_SHAPE_NULL(logits_shape, kernel_name, "logits") ||
                     CHECK_SHAPE_NULL(labels_shape, kernel_name, "labels") ||
                     CHECK_SHAPE_NULL(output_shape, kernel_name, "output");
    if (is_null_input_) {
      InitSizeLists();
      return true;
    }
    for (size_t i = 0; i < logits_shape.size(); i++) {
      logits_size_ *= logits_shape[i];
    }

    for (size_t i = 0; i < labels_shape.size(); i++) {
      labels_size_ *= labels_shape[i];
    }

    for (size_t i = 0; i < output_shape.size(); i++) {
      outputs_size_ *= output_shape[i];
    }

    InitSizeLists();
    return true;
  }

 protected:
  void InitSizeLists() override {
    input_size_list_.push_back(logits_size_);
    input_size_list_.push_back(labels_size_);
    input_size_list_.push_back(logits_size_);
    output_size_list_.push_back(outputs_size_);
  }

 private:
  size_t logits_size_;
  size_t labels_size_;
  size_t outputs_size_;
  bool is_null_input_;
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_SIGMOID_CROSS_ENTROPY_WITH_LOGITS_GRAD_GPU_KERNEL_H_
