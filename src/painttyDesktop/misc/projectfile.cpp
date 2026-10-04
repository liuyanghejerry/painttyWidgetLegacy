#include "projectfile.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace {
const QByteArray Magic("PAINTTY\n");
constexpr qint64 MaxFileBytes = 512LL * 1024 * 1024;
constexpr qint64 MaxDecodedBytes = 1024LL * 1024 * 1024;
constexpr quint32 MaxMetadataBytes = 1024 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool validate(const PaintingProject &project, QString *error)
{
    if (!ProjectFile::validSize(project.size) || project.layers.isEmpty()
            || project.layers.size() > ProjectFile::MaxLayers
            || project.selectedLayer < 0 || project.selectedLayer >= project.layers.size())
        return fail(error, QCoreApplication::translate("ProjectFile", "Invalid canvas size, layer count or selected layer."));
    QSet<QString> names;
    qint64 decodedBytes = 0;
    for (const auto &layer : project.layers) {
        if (layer.name.trimmed().isEmpty() || layer.name.size() > 256 || names.contains(layer.name)
                || (!layer.image.isNull() && layer.image.size() != project.size))
            return fail(error, QCoreApplication::translate("ProjectFile", "Invalid or duplicate layer name, or mismatched image size."));
        names.insert(layer.name);
        decodedBytes += layer.image.isNull() ? 0 : qint64(project.size.width()) * project.size.height() * 4;
    }
    if (decodedBytes > MaxDecodedBytes)
        return fail(error, QCoreApplication::translate("ProjectFile", "Project layer images exceed the 1 GiB memory limit."));
    return true;
}

bool readBlock(QFile &file, QDataStream &stream, QByteArray *data, quint32 limit)
{
    quint32 size = 0;
    stream >> size;
    if (stream.status() != QDataStream::Ok || size > limit || size > file.bytesAvailable())
        return false;
    *data = file.read(size);
    return data->size() == size;
}

bool decodeImage(QByteArray &data, const QSize &size, QImage *image, QString *error)
{
    QImageReader::setAllocationLimit(256);
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    if (reader.size() != size)
        return fail(error, QCoreApplication::translate("ProjectFile", "A layer image has the wrong canvas dimensions."));
    *image = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image->isNull()) return fail(error, reader.errorString());
    return true;
}

bool loadLegacy(const QString &path, PaintingProject *result, QString *error)
{
    QFile file(QDir(path).filePath("metadata.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxMetadataBytes)
        return fail(error, QCoreApplication::translate("ProjectFile", "Could not read legacy project metadata."));
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()
            || doc.object().value("version").toInt() != 1)
        return fail(error, QCoreApplication::translate("ProjectFile", "Invalid legacy project metadata."));
    PaintingProject project;
    project.size = QSize(doc.object().value("canvasWidth").toInt(), doc.object().value("canvasHeight").toInt());
    if (!ProjectFile::validSize(project.size)) return fail(error, QCoreApplication::translate("ProjectFile", "Invalid canvas dimensions."));
    const QDir images(QDir(path).filePath("images"));
    QStringList files = images.entryList({"layer_*.png"}, QDir::Files);
    const QRegularExpression pattern("^layer_([0-9]+)\\.png$");
    for (const auto &name : files)
        if (!pattern.match(name).hasMatch()) return fail(error, QCoreApplication::translate("ProjectFile", "Invalid legacy layer filename."));
    std::sort(files.begin(), files.end(), [&pattern](const QString &a, const QString &b) {
        return pattern.match(a).captured(1).toLongLong() < pattern.match(b).captured(1).toLongLong();
    });
    if (files.size() > ProjectFile::MaxLayers
            || qint64(project.size.width()) * project.size.height() * 4 * files.size() > MaxDecodedBytes)
        return fail(error, QCoreApplication::translate("ProjectFile", "Legacy project contains too many or too large layers."));
    for (const auto &name : files) {
        QFile imageFile(images.filePath(name));
        if (!imageFile.open(QIODevice::ReadOnly) || imageFile.size() > MaxFileBytes)
            return fail(error, QCoreApplication::translate("ProjectFile", "Could not read legacy layer image."));
        QByteArray data = imageFile.readAll();
        ProjectLayer layer;
        layer.name = QCoreApplication::translate("ProjectFile", "Layer %1").arg(project.layers.size() + 1);
        if (!decodeImage(data, project.size, &layer.image, error)) return false;
        project.layers.append(layer);
    }
    if (project.layers.isEmpty()) project.layers.append({QCoreApplication::translate("ProjectFile", "Layer 1"), QImage(), true, false});
    project.selectedLayer = project.layers.size() - 1;
    if (!validate(project, error)) return false;
    *result = project;
    return true;
}
}

bool ProjectFile::validSize(const QSize &size)
{
    return size.width() > 0 && size.height() > 0 && size.width() <= 10000 && size.height() <= 10000
            && qint64(size.width()) * size.height() <= 64LL * 1024 * 1024;
}

bool ProjectFile::save(const QString &path, const PaintingProject &project, QString *error)
{
    if (!validate(project, error)) return false;
    QJsonArray layers;
    for (const auto &layer : project.layers)
        layers.append(QJsonObject{{"name", layer.name}, {"visible", layer.visible}, {"locked", layer.locked}});
    const QByteArray metadata = QJsonDocument(QJsonObject{
        {"version", 2}, {"canvasWidth", project.size.width()}, {"canvasHeight", project.size.height()},
        {"selectedLayer", project.selectedLayer}, {"layers", layers}
    }).toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    file.write(Magic);
    stream << quint32(metadata.size());
    file.write(metadata);
    for (const auto &layer : project.layers) {
        QByteArray png;
        if (!layer.image.isNull()) {
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (!layer.image.save(&buffer, "PNG")) return fail(error, QCoreApplication::translate("ProjectFile", "Could not encode layer image."));
        }
        if (file.pos() + png.size() + 4 > MaxFileBytes)
            return fail(error, QCoreApplication::translate("ProjectFile", "Project exceeds the 512 MiB file size limit."));
        stream << quint32(png.size());
        if (file.write(png) != png.size()) return fail(error, file.errorString());
    }
    if (stream.status() != QDataStream::Ok || !file.commit()) return fail(error, file.errorString());
    return true;
}

bool ProjectFile::load(const QString &path, PaintingProject *result, QString *error)
{
    if (QFileInfo(path).isDir()) return loadLegacy(path, result, error);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > MaxFileBytes || file.read(Magic.size()) != Magic)
        return fail(error, QCoreApplication::translate("ProjectFile", "This is not a supported Mr.Paint project file."));
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    QByteArray metadata;
    if (!readBlock(file, stream, &metadata, MaxMetadataBytes))
        return fail(error, QCoreApplication::translate("ProjectFile", "Truncated or invalid project metadata."));
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(metadata, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()
            || doc.object().value("version").toInt() != 2)
        return fail(error, QCoreApplication::translate("ProjectFile", "Invalid or unsupported project version."));
    const auto object = doc.object();
    PaintingProject project;
    project.size = QSize(object.value("canvasWidth").toInt(), object.value("canvasHeight").toInt());
    project.selectedLayer = object.value("selectedLayer").toInt(-1);
    const auto layers = object.value("layers").toArray();
    if (!validSize(project.size) || layers.isEmpty() || layers.size() > MaxLayers)
        return fail(error, QCoreApplication::translate("ProjectFile", "Invalid canvas dimensions or layer count."));
    qint64 decodedBytes = 0;
    for (const auto &entry : layers) {
        if (!entry.isObject()) return fail(error, QCoreApplication::translate("ProjectFile", "Invalid layer metadata."));
        const auto info = entry.toObject();
        ProjectLayer layer;
        layer.name = info.value("name").toString();
        layer.visible = info.value("visible").toBool(true);
        layer.locked = info.value("locked").toBool(false);
        QByteArray png;
        if (!readBlock(file, stream, &png, quint32(MaxFileBytes)))
            return fail(error, QCoreApplication::translate("ProjectFile", "Truncated layer image."));
        if (!png.isEmpty()) {
            decodedBytes += qint64(project.size.width()) * project.size.height() * 4;
            if (decodedBytes > MaxDecodedBytes) return fail(error, QCoreApplication::translate("ProjectFile", "Project images exceed the memory limit."));
            if (!decodeImage(png, project.size, &layer.image, error)) return false;
        }
        project.layers.append(layer);
    }
    if (!file.atEnd() || !validate(project, error)) return fail(error, QCoreApplication::translate("ProjectFile", "Invalid project contents."));
    *result = project; // Never change the caller's document on failure.
    return true;
}
