#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "IMaintenanceStore.h"
#include "MaintenanceReport.h"
#include "MaintenanceRequest.h"
#include "config/MemoryConfig.h"
#include "vector/VectorMemoryIndexAdapter.h"

namespace dasall::memory {

class MemoryMaintenanceWorker {
 public:
  MemoryMaintenanceWorker(IMaintenanceStore& store,
                          MemoryConfig config,
                          VectorMemoryIndexAdapter* vector_adapter = nullptr,
                          std::shared_ptr<std::mutex> writer_mutex = nullptr);
  ~MemoryMaintenanceWorker();

  void start();
  void stop();

  [[nodiscard]] MaintenanceReport execute(const MaintenanceRequest& request);

 private:
  void background_loop();

  IMaintenanceStore& store_;
  MemoryConfig config_{};
  VectorMemoryIndexAdapter* vector_adapter_ = nullptr;
  std::shared_ptr<std::mutex> writer_mutex_;
  std::mutex schedule_mutex_;
  std::condition_variable schedule_cv_;
  std::thread worker_thread_;
  bool started_ = false;
  bool stopped_ = false;
};

}  // namespace dasall::memory