#pragma once
#include "ara/com/core.hpp"

namespace ara::com {

// Access the single SOME/IP-backed adapter instance.
// Usage: Runtime rt(GetSomeipAdapter());
IAdapter& GetSomeipAdapter();

} // namespace ara::com
