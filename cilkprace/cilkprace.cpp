#include "cilkprace.h"

std::unique_ptr<CilkpraceImpl_t> tool =
std::make_unique<decltype(tool)::element_type>();

pstack_reducer parallel_execution;

