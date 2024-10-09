#pragma once

extern std::unique_ptr<std::ofstream> outf;

//#define OUTS_CERR

#ifdef OUTS_CERR
#define outs_red std::cout
#else
extern cilk::ostream_reducer<char> outs_red;
#endif
