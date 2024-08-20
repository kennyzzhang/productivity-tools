#include <cilk/cilk_api.h>
#include <cilk/ostream_reducer.h>
#include <csi/csi.h>
#include <fstream>
#include <iostream>
#include <memory>

#define TRACE_CALLS 1

#define CILKTOOL_API extern "C" __attribute__((visibility("default")))

class CilkgraphImpl_t {
  std::unique_ptr<std::ofstream> outf;
public:
  cilk::ostream_reducer<char> outs_red;

private:
  // Need to manually register reducer
  //
  // > warning: reducer callbacks not implemented for structure members
  // > [-Wcilk-ignored]
  struct {
    template <class T>
    static void reducer_register(T& red) {
      __cilkrts_reducer_register(&red, sizeof(red),
          &std::decay_t<decltype(*&red)>::identity,
          &std::decay_t<decltype(*&red)>::reduce);
    }

    template <class T>
    static void reducer_unregister(T& red) {
      __cilkrts_reducer_unregister(&red);
    }

    struct RAII {
      CilkgraphImpl_t& this_;

      RAII(decltype(this_) this_) : this_(this_) {
        reducer_register(this_.outs_red);
        reducer_register(this_.callg);
      }

      ~RAII() {
        reducer_unregister(this_.outs_red);
        reducer_unregister(this_.callg);
      }
    } raii;
  } register_reducers = {.raii{*this}};

public:
  CilkgraphImpl_t() :
    outs_red([this]() -> std::basic_ostream<char>& {
      const char* envstr = getenv("CILKSCALE_OUT");
      if (envstr)
        return *(outf = std::make_unique<std::ofstream>(envstr));
      return std::cout;
    }()) // Not only are reducer callbacks not implemented, the hyperobject
         // is not even default constructed unless explicitly constructed.
  {}

  ~CilkgraphImpl_t() {
  }
};

static std::unique_ptr<CilkgraphImpl_t> tool =
    std::make_unique<decltype(tool)::element_type>();

static unsigned worker_number() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  return __cilkrts_get_worker_number();
#pragma clang diagnostic pop
}

CILKTOOL_API void __csi_init() {
}

CILKTOOL_API void __csi_unit_init(const char* const file_name,
                                  const instrumentation_counts_t counts) {
}

CILKTOOL_API
    void __csi_bb_entry(const csi_id_t bb_id, const bb_prop_t prop) {
  return;
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] bb_entry(bbid=" << bb_id << ")"
      << std::endl;
#endif
}

CILKTOOL_API
void __csi_bb_exit(const csi_id_t bb_id, const bb_prop_t prop) {
  return;
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] bb_exit(bbid=" << bb_id << ")"
      << std::endl;
#endif
}

CILKTOOL_API
void __csi_func_entry(const csi_id_t func_id, const func_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] func(fid=" << func_id << ", nsr="
      << prop.num_sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_func_exit(const csi_id_t func_exit_id, const csi_id_t func_id,
                     const func_exit_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] func_exit(feid=" << func_exit_id
      << ", fid=" << func_id << ")" << std::endl;
#endif
}

CILKTOOL_API void __csi_task(const csi_id_t task_id, const csi_id_t detach_id,
                             const task_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] task(tid=" << task_id << ", did="
      << detach_id << ", nsr=" << prop.num_sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_task_exit(const csi_id_t task_exit_id, const csi_id_t task_id,
                     const csi_id_t detach_id, const unsigned sync_reg,
                     const task_exit_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] task_exit(teid=" << task_exit_id
      << ", tid=" << task_id << ", did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_detach(const csi_id_t detach_id, const unsigned sync_reg,
                  const detach_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] detach(did=" << detach_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_detach_continue(const csi_id_t detach_continue_id,
                           const csi_id_t detach_id, const unsigned sync_reg,
                           const detach_continue_prop_t prop) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] detach_continue(dcid="
      << detach_continue_id << ", did=" << detach_id << ", sr=" << sync_reg
      << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_before_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] before_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}

CILKTOOL_API
void __csi_after_sync(const csi_id_t sync_id, const unsigned sync_reg) {
#ifdef TRACE_CALLS
  tool->outs_red
      << "[W" << worker_number() << "] after_sync(sid=" << sync_id << ", sr="
      << sync_reg << ")" << std::endl;
#endif
}
