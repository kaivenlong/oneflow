#ifndef ONEFLOW_CORE_ACTOR_ACTOR_REGISTRY_H_
#define ONEFLOW_CORE_ACTOR_ACTOR_REGISTRY_H_

#include "oneflow/core/actor/actor.h"
#include "oneflow/core/common/task.pb.h"

namespace oneflow {

std::shared_ptr<Actor> ConstructActor(const TaskProto&);

void AddActorCreator(TaskType task_type, bool is_forward,
                     std::function<Actor*()> creator);

template<TaskType task_type, bool is_forward, typename ActorType>
struct ActorRegister {
  ActorRegister() {
    AddActorCreator(task_type, is_forward, []() { return new ActorType; });
  }
};

#define REGISTER_ACTOR(TaskType, IsForward, ActorType) \
  static ActorRegister<TaskType, IsForward, ActorType> g_##ActorType##_##IsForward##_register_var;

}  // namespace oneflow

#endif // ONEFLOW_CORE_ACTOR_ACTOR_REGISTRY_H_
