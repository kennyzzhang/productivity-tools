#include "cilkprace.h"

std::unique_ptr<std::ofstream> outf;
#ifndef OUTS_CERR
cilk::ostream_reducer<char> outs_red([]() -> std::basic_ostream<char>& {
            const char* envstr = getenv("CILKSCALE_OUT");
            if (envstr)
            return *(outf = std::make_unique<std::ofstream>(envstr));
            return std::cout;
            }());
#endif

std::unique_ptr<CilkpraceImpl_t> tool =
std::make_unique<decltype(tool)::element_type>();


void init_pstack(void* view) {
  #if TRACE_CALLS
    std::cerr << "init pstack" << std::endl;
  #endif
  new (view) pstack();
}
void reduce_pstack (void* left_view, void* right_view) {
  #if TRACE_CALLS
    std::cerr << "reduce pstack" << std::endl;
  #endif
  pstack *left = static_cast<pstack*>(left_view);
  pstack *right = static_cast<pstack*>(right_view);
  assert(right->size() == 0 && "Expected empty pstack!");
  right->~pstack();
}

pstack_reducer parallel_execution;

