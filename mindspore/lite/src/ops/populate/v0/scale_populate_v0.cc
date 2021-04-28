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

#include "schema/model_v0_generated.h"
#include "src/ops/populate/populate_register.h"
#include "nnacl/scale.h"

namespace mindspore {
namespace lite {
namespace {
OpParameter *PopulateScaleParameter(const void *prim) {
  auto *primitive = static_cast<const schema::v0::Primitive *>(prim);
  MS_ASSERT(primitive != nullptr);
  auto scale_prim = primitive->value_as_Scale();
  if (scale_prim == nullptr) {
    MS_LOG(ERROR) << "scale_prim is nullptr";
    return nullptr;
  }
  auto *scale_param = reinterpret_cast<ScaleParameter *>(malloc(sizeof(ScaleParameter)));
  if (scale_param == nullptr) {
    MS_LOG(ERROR) << "malloc ScaleParameter failed.";
    return nullptr;
  }
  memset(scale_param, 0, sizeof(ScaleParameter));
  scale_param->op_parameter_.type_ = schema::PrimitiveType_ScaleFusion;

  scale_param->axis_ = scale_prim->axis();
  scale_param->activation_type_ = scale_prim->activationType();
  return reinterpret_cast<OpParameter *>(scale_param);
}
}  // namespace

Registry g_scaleV0ParameterRegistry(schema::v0::PrimitiveType_Scale, PopulateScaleParameter, SCHEMA_V0);
}  // namespace lite
}  // namespace mindspore
