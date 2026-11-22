#ifndef PSDEXPORT_H
#define PSDEXPORT_H

#include "projectfile.h"

class QIODevice;

bool writePsd(QIODevice &file, const QList<ProjectLayer> &layers, const QImage &composite);

#endif // PSDEXPORT_H
