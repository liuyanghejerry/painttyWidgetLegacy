#ifndef PROJECTFILE_H
#define PROJECTFILE_H

#include <QImage>
#include <QList>
#include <QString>

struct ProjectLayer
{
    QString name;
    QImage image; // Null means an empty, transparent layer.
    bool visible = true;
    bool locked = false;
};

struct PaintingProject
{
    QSize size;
    QList<ProjectLayer> layers; // Bottom to top.
    int selectedLayer = 0;
};

namespace ProjectFile
{
constexpr int MaxLayers = 256;
bool validSize(const QSize &size);
bool save(const QString &path, const PaintingProject &project, QString *error);
bool load(const QString &path, PaintingProject *project, QString *error);
}

#endif
