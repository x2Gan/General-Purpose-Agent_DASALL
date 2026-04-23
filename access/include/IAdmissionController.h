#pragma once

#include <string>

#include "AccessTypes.h"

namespace dasall::access {

class IAdmissionController {
 public:
  virtual ~IAdmissionController() = default;

  // 执行 Admission 判定，返回准入/重放/冲突等结果。
  virtual AccessAdmissionResult admit(const RuntimeDispatchRequest& request) = 0;

  // 请求在 dispatch 前失败或提前结束时释放 inflight 票据。
  virtual void release_ticket(const std::string& ticket_ref) = 0;

  // 请求生命周期完成后回写 completion 结果。
  virtual void record_completion(
	  const std::string& ticket_ref,
	  const RuntimeDispatchResult& result) = 0;
};

}  // namespace dasall::access
