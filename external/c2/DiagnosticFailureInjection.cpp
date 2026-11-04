#include "DiagnosticFailureInjection.h"
std::atomic<int> DiagnosticFailureInjection::create{0};
std::atomic<int> DiagnosticFailureInjection::render{0};
std::atomic<bool> DiagnosticFailureInjection::blockNextRetire{false};
std::atomic<int> DiagnosticFailureInjection::frees{0};
std::atomic<int> DiagnosticFailureInjection::callbacks{0};
std::atomic<bool> DiagnosticFailureInjection::holdCallback{false};
std::atomic<bool> DiagnosticFailureInjection::callbackWaiting{false};
