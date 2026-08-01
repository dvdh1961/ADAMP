#ifndef CRASHTRACE_H
#define CRASHTRACE_H

#include <QString>

namespace CrashTrace {

void install();
void mark(const QString& message);
void cleanExit();
QString traceFilePath();
bool previousRunCrashed();

}

#endif // CRASHTRACE_H
