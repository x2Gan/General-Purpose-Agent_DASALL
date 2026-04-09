#pragma once

#include "ServiceTypes.h"

namespace dasall::services {

class IDataService {
public:
	virtual ~IDataService() = default;

	virtual DataQueryResult query(const DataQueryRequest& request) = 0;
	virtual DataCatalogResult list_capabilities(const DataCatalogRequest& request) = 0;
};

}  // namespace dasall::services