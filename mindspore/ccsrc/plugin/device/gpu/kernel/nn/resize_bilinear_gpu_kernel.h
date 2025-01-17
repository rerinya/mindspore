/**
 * Copyright 2021-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_RESIZE_BILINEAR_GPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_RESIZE_BILINEAR_GPU_KERNEL_H_

#include <vector>
#include "plugin/device/gpu/kernel/gpu_kernel.h"
#include "plugin/device/gpu/kernel/gpu_kernel_factory.h"
#include "plugin/device/gpu/kernel/cuda_impl/cuda_ops/resize_bilinear_impl.cuh"

namespace mindspore {
namespace kernel {
template <typename T>
class ResizeBilinearGpuKernelMod : public DeprecatedNativeGpuKernelMod {
 public:
  ResizeBilinearGpuKernelMod() { ResetResource(); }
  ~ResizeBilinearGpuKernelMod() override = default;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &workspace,
              const std::vector<AddressPtr> &outputs, void *stream_ptr) override {
    if (is_null_input_) {
      return true;
    }
    T *input = GetDeviceAddress<T>(inputs, 0);
    T *output = GetDeviceAddress<T>(outputs, 0);
    float h_scale = Scaling(input_h_, output_h_, align_corners_);
    float w_scale = Scaling(input_w_, output_w_, align_corners_);
    CalResizeBilinear(input, n_, c_, input_h_, input_w_, output_h_, output_w_, h_scale, w_scale, half_pixel_centers_,
                      output, device_id_, reinterpret_cast<cudaStream_t>(stream_ptr));
    return true;
  }

  bool Init(const CNodePtr &kernel_node) override {
    device_id_ = MsContext::GetInstance()->get_param<uint32_t>(MS_CTX_DEVICE_ID);
    auto kernel_name = common::AnfAlgo::GetCNodeName(kernel_node);
    size_t input_num = common::AnfAlgo::GetInputTensorNum(kernel_node);
    kernel_node_ = kernel_node;
    constexpr size_t kDynamicSizeInputNum = 2;
    if (input_num != 1 && input_num != kDynamicSizeInputNum) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name << "', the number of inputs must be 1 or " << kDynamicSizeInputNum
                        << ", but got " << input_num;
    }
    size_t output_num = common::AnfAlgo::GetOutputTensorNum(kernel_node);
    if (output_num != 1) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name << "', the number of outputs must be 1, but got " << output_num;
    }
    auto input_shape = AnfAlgo::GetInputDeviceShapeAdaptively(kernel_node, 0);
    auto output_shape = AnfAlgo::GetOutputDeviceShapeAdaptively(kernel_node, 0);
    is_null_input_ =
      CHECK_SHAPE_NULL(input_shape, kernel_name, "input") || CHECK_SHAPE_NULL(output_shape, kernel_name, "output");
    if (is_null_input_) {
      InitSizeLists();
      return true;
    }
    if (input_shape.size() != 4 || output_shape.size() != 4) {
      MS_LOG(EXCEPTION) << "For '" << kernel_name << "', the dimension of input and output must be equal to 4, but "
                        << "got the dimension of input: " << input_shape.size()
                        << ", the dimension of output: " << output_shape.size();
    }
    n_ = LongToInt(input_shape[0]);
    c_ = LongToInt(input_shape[1]);
    input_h_ = LongToInt(input_shape[2]);
    input_w_ = LongToInt(input_shape[3]);
    output_h_ = LongToInt(output_shape[2]);
    output_w_ = LongToInt(output_shape[3]);
    input_size_ = sizeof(T) * SizeOf(input_shape);
    output_size_ = sizeof(T) * SizeOf(output_shape);
    align_corners_ = GetAttr<bool>(kernel_node, "align_corners");
    half_pixel_centers_ = GetAttr<bool>(kernel_node, "half_pixel_centers");
    InitSizeLists();
    return true;
  }

  void ResetResource() noexcept override {
    align_corners_ = false;
    half_pixel_centers_ = false;
    is_null_input_ = false;
    n_ = 0;
    c_ = 0;
    input_h_ = 0;
    input_w_ = 0;
    output_h_ = 0;
    output_w_ = 0;
    input_size_ = 0;
    output_size_ = 0;
    workspace_size_ = 0;
    input_size_list_.clear();
    output_size_list_.clear();
    workspace_size_list_.clear();
  }

 protected:
  void InitSizeLists() override {
    input_size_list_.push_back(input_size_);
    output_size_list_.push_back(output_size_);
  }

 private:
  float Scaling(const int in_size, const int out_size, bool align_corners) {
    return (align_corners && out_size > 1) ? (in_size - 1) / static_cast<float>(out_size - 1)
                                           : in_size / static_cast<float>(out_size);
  }

  bool align_corners_;
  bool half_pixel_centers_;
  bool is_null_input_;
  int n_;
  int c_;
  int input_h_;
  int input_w_;
  int output_h_;
  int output_w_;
  size_t input_size_;
  size_t output_size_;
  size_t workspace_size_;
};
}  // namespace kernel
}  // namespace mindspore
#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_GPU_NN_RESIZE_BILINEAR_GPU_KERNEL_H_
