#ifndef _SHADOW_LABEL_H
#define _SHADOW_LABEL_H

#if defined(CILKPRACE_USE_LEB8_PTR)
#include "leb8-ptr.h"
#elif defined(CILKPRACE_USE_LEB8_SHORT_PTR)
#include "leb8-short-ptr.h"
#elif defined(USE_OS_LABEL_STRING)
#include "string-label.h"
#elif defined(USE_OS_LABEL_LEB8_RANGE)
#include "leb8-range.h"
#else
#include "leb8-single.h"
#endif

#endif /* _SHADOW_LABEL_H */
