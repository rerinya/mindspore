/**
 * Copyright 2022 Huawei Technologies Co., Ltd
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

#include "runtime/graph_scheduler/optimizer/multi_actor_fusion.h"
#include <vector>
#include "runtime/graph_scheduler/scheduler_helper.h"

namespace mindspore {
namespace runtime {
constexpr size_t kActorFusionMaxNum = 2;

void MultiActorFusion::Process(ActorSet *const actor_set, AbstractActor *const) {
  MS_EXCEPTION_IF_NULL(actor_set);
  if ((actor_set->control_actors_ != nullptr) || (!actor_set->custom_actors_.empty())) {
    return;
  }

  if (!AnalyzeDependency(actor_set)) {
    MS_LOG(INFO) << "The dependency of " << actor_set->name_
                 << " is too complicated and the pass MultiActorFusion is skipped.";
    return;
  }

  FuseMultiActors(actor_set);

  // Link fusion actor.
  for (auto &fusion_actor : actor_set->fusion_actors_) {
    SchedulerHelper::AddArrowForFusionActor(fusion_actor.get());
  }
}

bool MultiActorFusion::AnalyzeDependency(const ActorSet *actor_set) {
  MS_EXCEPTION_IF_NULL(actor_set);
  auto need_processed_actors = SchedulerHelper::CollectActors(actor_set);
  // The second of pair indicates whether the actor finishes adding the dependency.
  mindspore::HashMap<std::string, std::pair<AbstractActor *, bool>> actor_infos;
  for (auto &actor : need_processed_actors) {
    MS_EXCEPTION_IF_NULL(actor);
    actor_infos[actor->GetAID().Name()] = std::make_pair(actor.get(), false);
  }

  std::vector<AbstractActorPtr> unprocessed_actors;
  while (!need_processed_actors.empty()) {
    MS_LOG(INFO) << actor_set->name_ << " analyze dependency and process actors num: " << need_processed_actors.size();
    for (auto &actor : need_processed_actors) {
      MS_EXCEPTION_IF_NULL(actor);
      auto &current_actor_info = actor_infos[actor->GetAID().Name()];
      // Maybe finish adding the dependency in the function AddDependency.
      if (current_actor_info.second) {
        continue;
      }

      // Collect the input actor infos from the input data and input control.
      std::set<std::pair<AbstractActor *, bool>> input_actor_infos;
      for (auto &input_data_arrow_aid : actor->input_data_arrow_aids()) {
        (void)input_actor_infos.insert(actor_infos[input_data_arrow_aid.first.Name()]);
      }
      for (auto &input_control_arrow_aid : actor->input_control_arrow_aids()) {
        (void)input_actor_infos.insert(actor_infos[input_control_arrow_aid.first.Name()]);
      }

      // Add the dependency from the input actor info.
      current_actor_info.second = true;
      for (auto &input_actor_info : input_actor_infos) {
        if (!AddDependency(const_cast<std::pair<AbstractActor *, bool> *>(&input_actor_info), &actor_infos)) {
          (void)unprocessed_actors.emplace_back(actor);
          current_actor_info.second = false;
          break;
        }
        SchedulerHelper::AddDependency(actor.get(), input_actor_info.first);
      }
    }

    // This iteration doesn't process any actor and need stop.
    if (need_processed_actors.size() == unprocessed_actors.size()) {
      return false;
    }
    // Updata the actors which need be processed in the next iteration.
    need_processed_actors.assign(unprocessed_actors.begin(), unprocessed_actors.end());
    unprocessed_actors.clear();
  }

  return true;
}

bool MultiActorFusion::AddDependency(
  std::pair<AbstractActor *, bool> *const actor_info,
  mindspore::HashMap<std::string, std::pair<AbstractActor *, bool>> *const actor_infos) {
  MS_EXCEPTION_IF_NULL(actor_info);
  MS_EXCEPTION_IF_NULL(actor_infos);
  if (actor_info->second) {
    return true;
  }

  // Collect the input actor infos from the input data and input control.
  std::set<std::pair<AbstractActor *, bool>> input_actor_infos;
  for (auto &input_data_arrow_aid : actor_info->first->input_data_arrow_aids()) {
    (void)input_actor_infos.insert(actor_infos->at(input_data_arrow_aid.first.Name()));
  }
  for (auto &input_control_arrow_aid : actor_info->first->input_control_arrow_aids()) {
    (void)input_actor_infos.insert(actor_infos->at(input_control_arrow_aid.first.Name()));
  }

  // Add the dependency from the input actor info.
  for (auto &input_actor_info : input_actor_infos) {
    if (!input_actor_info.second) {
      return false;
    }
    SchedulerHelper::AddDependency(actor_info->first, input_actor_info.first);
  }

  actor_info->second = true;
  return true;
}

void MultiActorFusion::FuseMultiActors(ActorSet *const actor_set) {
  MS_EXCEPTION_IF_NULL(actor_set);
  // Build all the fusion actors.
  std::vector<AbstractActorPtr> actors;
  for (auto &kernel_actor : actor_set->kernel_actors_) {
    MS_EXCEPTION_IF_NULL(kernel_actor);
    (void)actors.emplace_back(kernel_actor);
    if (actors.size() % kActorFusionMaxNum == 0) {
      auto fusion_actor = SchedulerHelper::BuildMultiActors(actors);
      (void)actor_set->fusion_actors_.emplace_back(fusion_actor);
      actors.clear();
    }
  }
  if (actors.size() > 1) {
    auto fusion_actor = SchedulerHelper::BuildMultiActors(actors);
    (void)actor_set->fusion_actors_.emplace_back(fusion_actor);
  }
}
}  // namespace runtime
}  // namespace mindspore
