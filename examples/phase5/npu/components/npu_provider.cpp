#include "npu_provider.h"

#include "gfsim/components.h"

static_assert(gfsim::Component<phase5::npu::provider::NpuProvider>);
static_assert(gfsim::Component<phase5::npu::provider::NpuNodeProvider>);
