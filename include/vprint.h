#include <cstdarg>
#include <cstdio>

extern bool verboseOutput;

void verboseprint(const char *format, ...)
{
  if (!verboseOutput) return;
  else {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}