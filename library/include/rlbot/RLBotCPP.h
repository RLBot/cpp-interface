#pragma once

#ifdef _WIN32
#ifdef RLBotCPP_EXPORTS
#define RLBotCPP_API __declspec (dllexport)
#else
#define RLBotCPP_API
#endif
#else
#define RLBotCPP_API __attribute__ ((visibility ("default")))
#endif
