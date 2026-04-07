#pragma once

#ifdef _WIN32
#ifdef RLBot_EXPORTS
#define RLBot_API __declspec (dllexport)
#else
#define RLBot_API
#endif
#else
#define RLBot_API __attribute__ ((visibility ("default")))
#endif
