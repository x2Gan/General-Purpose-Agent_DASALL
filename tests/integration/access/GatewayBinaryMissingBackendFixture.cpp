#include <iostream>

#include "AccessGatewayFactory.h"

int main() {
  dasall::access::GatewayAccessPipelineOptions options;
  options.bootstrap_config.entry_type = "gateway";
  options.bootstrap_config.allowed_protocols = {"http"};

  auto gateway = dasall::access::create_gateway_access_gateway(std::move(options));
  if (!gateway->init()) {
    std::cerr << "[dasall_gateway] AccessGateway init failed: production submit pipeline unavailable\n";
    return 1;
  }

  std::cerr << "[dasall_gateway] unexpected init success without runtime backend\n";
  return 2;
}