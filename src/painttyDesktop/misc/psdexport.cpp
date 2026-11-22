#include "psdexport.h"

#include <QDataStream>
#include <QIODevice>
#include <array>
#include <limits>

namespace {
constexpr std::array<qint16, 4> ChannelIds{0, 1, 2, -1}; // RGBA, with -1 for layer transparency.
using ChannelOffsets = std::array<qint64, ChannelIds.size()>;

QByteArray channelPlane(const QImage &rgba, int channel)
{
    QByteArray plane(qsizetype(rgba.width()) * rgba.height(), Qt::Uninitialized);
    char *dst = plane.data();
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar *row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x)
            *dst++ = char(row[4 * x + channel]);
    }
    return plane;
}

QByteArray layerExtraData(const QString &name)
{
    QByteArray extra;
    QDataStream out(&extra, QIODevice::WriteOnly);
    out << quint32(0) << quint32(0); // Layer mask and blending ranges.

    const QByteArray legacyName = name.toLatin1().left(255);
    out << quint8(legacyName.size());
    out.writeRawData(legacyName.constData(), legacyName.size());
    while (extra.size() % 4) out << quint8(0);

    out.writeRawData("8BIMluni", 8);
    out << quint32(4 + 2 * name.size()) << quint32(name.size());
    for (QChar ch : name) out << quint16(ch.unicode());
    return extra;
}

ChannelOffsets writeLayerRecord(QDataStream &out, const ProjectLayer &layer)
{
    // A transparent pixel keeps an empty layer editable in readers that skip zero-area records.
    const QSize size = layer.image.isNull() ? QSize(1, 1) : layer.image.size();
    out << qint32(0) << qint32(0) << qint32(size.height()) << qint32(size.width());
    out << quint16(ChannelIds.size());
    ChannelOffsets offsets;
    for (size_t i = 0; i < ChannelIds.size(); ++i) {
        out << ChannelIds[i];
        offsets[i] = out.device()->pos();
        out << quint32(0); // Filled in after writing the channel.
    }
    out.writeRawData("8BIMnorm", 8);
    out << quint8(255) << quint8(0) << quint8(0) << quint8(0);
    const QByteArray extra = layerExtraData(layer.name);
    out << quint32(extra.size());
    out.writeRawData(extra.constData(), extra.size());
    return offsets;
}

bool setLength(QDataStream &out, qint64 field, qint64 length)
{
    auto *file = out.device();
    const qint64 end = file->pos();
    if (length < 0 || length > std::numeric_limits<quint32>::max() || !file->seek(field))
        return false;
    out << quint32(length);
    return out.status() == QDataStream::Ok && file->seek(end);
}
}

bool writePsd(QIODevice &file, const QList<ProjectLayer> &layers, const QImage &composite)
{
    if (layers.isEmpty() || layers.size() > ProjectFile::MaxLayers || composite.isNull()) return false;
    QDataStream out(&file); // PSD uses big-endian integers.
    out.writeRawData("8BPS", 4);
    out << quint16(1) << quint32(0) << quint16(0); // Version and six reserved bytes.
    out << quint16(ChannelIds.size()) << quint32(composite.height()) << quint32(composite.width());
    out << quint16(8) << quint16(3); // Eight-bit RGB.
    out << quint32(0) << quint32(0); // Color mode data and image resources.

    const qint64 maskLengthField = file.pos();
    out << quint32(0);
    const qint64 layersLengthField = file.pos();
    out << quint32(0) << qint16(-layers.size()); // The merged image includes transparency.
    QList<ChannelOffsets> offsets;
    for (const auto &layer : layers) offsets.append(writeLayerRecord(out, layer));

    QImage transparentPixel(1, 1, QImage::Format_RGBA8888);
    transparentPixel.fill(Qt::transparent);
    for (qsizetype i = 0; i < layers.size(); ++i) {
        const QImage rgba = layers[i].image.isNull() ? transparentPixel
                                                     : layers[i].image.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) return false;
        for (int channel = 0; channel < int(ChannelIds.size()); ++channel) {
            const qint64 start = file.pos();
            const QByteArray plane = channelPlane(rgba, channel);
            const QByteArray compressed = qCompress(plane);
            // qCompress prefixes its zlib stream with a four-byte uncompressed length.
            if (compressed.size() > 4 && compressed.size() - 4 < plane.size()) {
                out << quint16(2);
                out.writeRawData(compressed.constData() + 4, compressed.size() - 4);
            } else {
                out << quint16(0);
                out.writeRawData(plane.constData(), plane.size());
            }
            if (!setLength(out, offsets[i][channel], file.pos() - start)) return false;
        }
    }
    if ((file.pos() - layersLengthField - 4) % 2) out << quint8(0);
    if (!setLength(out, layersLengthField, file.pos() - layersLengthField - 4)) return false;
    out << quint32(0); // Global layer mask.
    if (!setLength(out, maskLengthField, file.pos() - maskLengthField - 4)) return false;

    const QImage rgba = composite.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) return false;
    out << quint16(0); // Raw merged image, in red, green, blue, alpha plane order.
    for (int channel = 0; channel < int(ChannelIds.size()); ++channel) {
        const QByteArray plane = channelPlane(rgba, channel);
        out.writeRawData(plane.constData(), plane.size());
    }
    return out.status() == QDataStream::Ok;
}
