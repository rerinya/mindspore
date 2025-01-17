/**
 * Copyright 2019-2022 Huawei Technologies Co., Ltd
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

#ifndef MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ELTWISE_CPU_KERNEL_H_
#define MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ELTWISE_CPU_KERNEL_H_

#include <memory>
#include <vector>
#include <map>
#include <string>
#include <utility>
#include "plugin/device/cpu/kernel/mkldnn/mkl_cpu_kernel.h"

namespace mindspore {
namespace kernel {
constexpr auto kUnKnown = "UnKnown";
class EltWiseCpuKernelMod : public MKLCpuKernelMod {
 public:
  EltWiseCpuKernelMod() = default;
  explicit EltWiseCpuKernelMod(const std::string &kernel_name) : kernel_name_(kernel_name) {}
  ~EltWiseCpuKernelMod() override = default;

  bool Init(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
            const std::vector<KernelTensorPtr> &outputs) override;

  int Resize(const BaseOperatorPtr &base_operator, const std::vector<KernelTensorPtr> &inputs,
             const std::vector<KernelTensorPtr> &outputs, const std::map<uint32_t, tensor::TensorPtr> &) override;

  bool Launch(const std::vector<AddressPtr> &inputs, const std::vector<AddressPtr> &,
              const std::vector<AddressPtr> &outputs) override {
    if (is_null_input_) {
      return true;
    }
    return kernel_func_(this, inputs, outputs);
  }

  std::vector<KernelAttr> GetOpSupport() override;

 private:
  bool LaunchKernel(const std::vector<kernel::AddressPtr> &inputs, const std::vector<kernel::AddressPtr> &outputs);
  using EltWiseFunc = std::function<bool(EltWiseCpuKernelMod *, const std::vector<kernel::AddressPtr> &,
                                         const std::vector<kernel::AddressPtr> &)>;
  static std::map<std::string, std::vector<std::pair<KernelAttr, EltWiseCpuKernelMod::EltWiseFunc>>> kernel_attr_map_;
  EltWiseFunc kernel_func_;
  dnnl::eltwise_forward::desc GetForwardEltwiseDesc(const dnnl::memory::desc src_desc);
  dnnl::prop_kind dnnl_forward_{dnnl::prop_kind::forward_training};
  std::string kernel_name_{kUnKnown};
  std::vector<size_t> src_shape_{};
  size_t input_element_num_{0};
  bool is_null_input_{false};
};
}  // namespace kernel
}  // namespace mindspore

#endif  // MINDSPORE_CCSRC_BACKEND_KERNEL_COMPILER_CPU_ELTWISE_CPU_KERNEL_H_
