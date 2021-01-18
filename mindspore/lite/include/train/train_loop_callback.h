/**
 * Copyright 2020 Huawei Technologies Co., Ltd
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
#ifndef MINDSPORE_LITE_INCLUDE_TRAIN_TRAIN_LOOP_CALLBACK_H_
#define MINDSPORE_LITE_INCLUDE_TRAIN_TRAIN_LOOP_CALLBACK_H_
#include <vector>
#include <string>
#include <tuple>
#include <unordered_map>

namespace mindspore {
namespace session {

class TrainSession;
class TrainLoop;

struct TrainLoopCallBackData {
  TrainLoopCallBackData(bool train_mode, int epoch, TrainSession *session, TrainLoop *loop)
      : train_mode_(train_mode), epoch_(epoch), session_(session), loop_(loop) {}

  bool train_mode_;       /**< training mode of TrainSession object */
  unsigned int epoch_;    /**< the current training epoch (starts at 0) */
  unsigned int step_ = 0; /**< the current step within the epoch */
  TrainSession *session_; /**< pointer to the TrainSession */
  TrainLoop *loop_;
};

constexpr int RET_CONTINUE = 0;
constexpr int RET_STOP_TRAINING = 1;
constexpr int RET_EXIT = 2;

class TrainLoopCallBack {
 public:
  virtual ~TrainLoopCallBack() = default;
  virtual void Begin(const TrainLoopCallBackData &cb_data) {}
  virtual void End(const TrainLoopCallBackData &cb_data) {}
  virtual void EpochBegin(const TrainLoopCallBackData &cb_data) {}
  virtual int EpochEnd(const TrainLoopCallBackData &cb_data) { return RET_CONTINUE; }
  virtual void StepBegin(const TrainLoopCallBackData &cb_data) {}
  virtual void StepEnd(const TrainLoopCallBackData &cb_data) {}
};

}  // namespace session
}  // namespace mindspore
#endif  // MINDSPORE_LITE_INCLUDE_TRAIN_TRAIN_LOOP_CALLBACK_H_
