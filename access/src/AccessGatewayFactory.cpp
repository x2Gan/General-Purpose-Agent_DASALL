#include "AccessGatewayFactory.h"

#include <memory>
#include <utility>

#include "AccessGateway.h"

namespace dasall::access {

std::shared_ptr<IAccessGateway> create_access_gateway(
    AccessGatewayFactoryOptions options) {
  return std::make_shared<AccessGateway>(std::move(options.submit_pipeline),
                                         std::move(options.publish_backend));
}

}  // namespace dasall::access