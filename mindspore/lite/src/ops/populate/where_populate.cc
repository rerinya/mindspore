/**
 * Copyright 2019-2021 Huawei Technologies Co., Ltd
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
#include "src/ops/populate/populate_register.h"
#include "nnacl/where_parameter.h"
using mindspore::schema::PrimitiveType_Where;

namespace mindspore {
namespace lite {
namespace {
OpParameter *PopulateWhereParameter(const void *prim) {
  auto *where_parameter = reinterpret_cast<WhereParameter *>(malloc(sizeof(WhereParameter)));
  if (where_parameter == nullptr) {
    MS_LOG(ERROR) << "malloc Where parameter failed.";
    return nullptr;
  }
  memset(where_parameter, 0, sizeof(OpParameter));
  auto primitive = static_cast<const schema::Primitive *>(prim);
  MS_ASSERT(primitive != nullptr);
  where_parameter->op_parameter_.type_ = primitive->value_type();
  return reinterpret_cast<OpParameter *>(where_parameter);
}
}  // namespace
REG_POPULATE(PrimitiveType_Where, PopulateWhereParameter, SCHEMA_CUR)
}  // namespace lite
}  // namespace mindspore
