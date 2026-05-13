#include "archivefile.h"
#include "../common/common.h"
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QCryptographicHash>
#include <QSettings>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>

ArchiveFile::ArchiveFile(const QString& name,
                         const QString& signature,
                         QObject *parent) :
    QObject(parent),
    signature_(signature),
    backend_(nullptr),
    line_count_(0)
{
    if(!name.isEmpty())
        setName(name);
}

ArchiveFile::ArchiveFile(QObject *parent) :
    QObject(parent),
    signature_(""),  // 显式初始化为空字符串
    backend_(nullptr),
    line_count_(0)
{
}

ArchiveFile::~ArchiveFile()
{
    if(backend_ && backend_->isOpen()){
        backend_->flush();
        backend_->waitForBytesWritten(3000);
        backend_->close();
    }
}

QByteArray ArchiveFile::readAll() const
{
    if(!backend_)
        return QByteArray();
    backend_->flush();
    backend_->seek(0);
    return backend_->readAll();
}

QList<QByteArray> ArchiveFile::readAllLines() const
{
    QList<QByteArray> lines;
    if (!backend_)
        return lines;
    backend_->flush();
    backend_->seek(0);
    while (!backend_->atEnd()) {
        QByteArray line = backend_->readLine();
        if (!line.isEmpty()) {
            lines.append(line.trimmed());
        }
    }
    qDebug() << "[ArchiveFile] 从文件" << backend_->fileName() << "中读取了" << lines.size()
             << "行数据";
    return lines;
}

quint64 ArchiveFile::lineCount() const
{
    return line_count_;
}

void ArchiveFile::appendData(const QByteArray &data)
{
    if(!backend_) {
        qWarning() << "[ArchiveFile] 无法追加数据：backend_为nullptr，ArchiveFile可能未正确初始化";
        return;
    }
    
    // 检查文件是否打开，如果未打开则重新打开
    if (!backend_->isOpen()) {
        qWarning() << "[ArchiveFile] 文件未打开，尝试重新打开文件";
        if (!backend_->open(QIODevice::ReadWrite | QIODevice::Append)) {
            qWarning() << "[ArchiveFile] 重新打开文件失败:" << backend_->errorString();
            return;
        }
    }
    
    backend_->seek(backend_->size());
    backend_->write(data);
    backend_->write("\n"); // 添加换行符
    line_count_++;
    saveMetadata();
    emit lineCountChanged(line_count_);
}

void ArchiveFile::appendArchiveData(const QByteArray &data)
{
    if(!backend_) {
        qWarning() << "[ArchiveFile] 无法追加archive数据：backend_为nullptr，ArchiveFile可能未正确初始化";
        return;
    }
    
    // 检查文件是否打开，如果未打开则重新打开
    if (!backend_->isOpen()) {
        qWarning() << "[ArchiveFile] 文件未打开，尝试重新打开文件";
        if (!backend_->open(QIODevice::ReadWrite | QIODevice::Append)) {
            qWarning() << "[ArchiveFile] 重新打开文件失败:" << backend_->errorString();
            return;
        }
    }
    
    backend_->seek(backend_->size());
    backend_->write(data);
    backend_->write("\n"); // 添加换行符
    line_count_++;
    saveMetadata();
    emit lineCountChanged(line_count_);
}

void ArchiveFile::setSignature(const QString& sign)
{
    qDebug()<<"old sign"<<signature_<<"new"<<sign;
    if(!signature_.isEmpty() && sign != signature_){
        resetForNewSignature(sign);
    } else {
        signature_ = sign;
        saveMetadata();
    }
}

void ArchiveFile::setLineCount(quint64 count)
{
    if(line_count_ != count) {
        line_count_ = count;
        saveMetadata();
        emit lineCountChanged(line_count_);
    }
}

bool ArchiveFile::isDataConsistent() const
{
    return isSignatureValid() && isLineCountValid();
}

bool ArchiveFile::isSignatureValid() const
{
    return !signature_.isEmpty();
}

bool ArchiveFile::isLineCountValid() const
{
    return line_count_ > 0;
}

void ArchiveFile::resetForNewSignature(const QString &signature)
{
    qDebug() << "[ArchiveFile] 重置为新签名:" << signature;
    
    // 清空现有数据
    prune();
    clearCachedImages();
    
    // 设置新签名
    signature_ = signature;
    line_count_ = 0;
    
    // 保存元数据
    saveMetadata();
    
    // 发送信号
    emit newSignature(signature_);
    emit lineCountChanged(line_count_);
}

void ArchiveFile::flush()
{
    if(!backend_)
        return;
    backend_->flush();
}

void ArchiveFile::prune()
{
    if(!backend_)
        return;
    qDebug()<<"File pruned";
    backend_->resize(0);
    backend_->seek(0);
    line_count_ = 0;
    saveMetadata();
}

void ArchiveFile::remove()
{
    if(!backend_)
        return;
    backend_->close();
    backend_->remove();
    clearCachedImages();
}

quint64 ArchiveFile::size() const
{
    if(!backend_)
        return 0;
    return backend_->size();
}

QString ArchiveFile::name() const
{
    return name_;
}

void ArchiveFile::setName(const QString &name)
{
    if(name.isEmpty() || name_ == name)
        return;
    name_ = name;
    createFile();
}

QString ArchiveFile::signature() const
{
    return signature_;
}

QString ArchiveFile::dirName() const
{
    return dir_name_;
}

bool ArchiveFile::saveSnapshotImages(const QList<QImage> &layerImages)
{
    if(dir_name_.isEmpty())
        return false;
        
    QString imagesPath = getImagesPath();
    QDir::current().mkpath(imagesPath);
    
    bool success = true;
    for(int i = 0; i < layerImages.size(); ++i) {
        QString imagePath = QString("%1/layer_%2.png").arg(imagesPath).arg(i);
        if(!layerImages[i].save(imagePath)) {
            qWarning() << "[ArchiveFile] 保存图层图片失败:" << imagePath;
            success = false;
        }
    }
    
    if(success) {
        qDebug() << "[ArchiveFile] 成功保存" << layerImages.size() << "个图层图片";
    }
    
    return success;
}

QList<QImage> ArchiveFile::loadSnapshotImages() const
{
    QList<QImage> images;
    
    if(dir_name_.isEmpty())
        return images;
        
    QString imagesPath = getImagesPath();
    QDir imagesDir(imagesPath);
    
    if(!imagesDir.exists())
        return images;
    
    QStringList filters;
    filters << "layer_*.png";
    QStringList imageFiles = imagesDir.entryList(filters, QDir::Files, QDir::Name);
    
    for(const QString &fileName : imageFiles) {
        QString imagePath = imagesDir.filePath(fileName);
        QImage image(imagePath);
        if(!image.isNull()) {
            images.append(image);
        } else {
            qWarning() << "[ArchiveFile] 加载图层图片失败:" << imagePath;
        }
    }
    
    qDebug() << "[ArchiveFile] 成功加载" << images.size() << "个图层图片";
    return images;
}

bool ArchiveFile::hasCachedImages() const
{
    if(dir_name_.isEmpty())
        return false;
        
    QString imagesPath = getImagesPath();
    QDir imagesDir(imagesPath);
    return imagesDir.exists() && !imagesDir.entryList(QStringList() << "layer_*.png", QDir::Files).isEmpty();
}

void ArchiveFile::clearCachedImages() const
{
    if(dir_name_.isEmpty())
        return;
        
    QString imagesPath = getImagesPath();
    QDir imagesDir(imagesPath);
    if(imagesDir.exists()) {
        imagesDir.removeRecursively();
        qDebug() << "[ArchiveFile] 清除缓存的图层图片";
    }
}

bool ArchiveFile::createFile()
{
    QCryptographicHash crypto(QCryptographicHash::Sha1);
    crypto.addData((name_).toUtf8());
    auto hash = crypto.result().toHex();
    dir_name_ = QString("%1/%2")
            .arg("cache")
            .arg(QString::fromUtf8(hash));

    auto isGood = QDir::current().mkpath(dir_name_);
    if(!isGood){
        qWarning()<<"Cannot create path: "<<dir_name_;
        return isGood;
    }
    QString filename = QString("%1/data")
            .arg(dir_name_);

    if(backend_){
        if(backend_->isOpen())
            backend_->close();
        backend_->deleteLater();
        backend_ = nullptr;
    }

    backend_ = new QFile(filename, this);
    isGood = backend_->open(QIODevice::ReadWrite);
    if(!isGood){
        qWarning()<<"Cannot open archive file:"<<filename;
    }

    // 加载元数据
    loadMetadata();

    return isGood;
}

void ArchiveFile::saveMetadata()
{
    if(dir_name_.isEmpty()) {
        qWarning() << "[ArchiveFile] 无法保存元数据：目录名为空，ArchiveFile可能未正确初始化";
        return;
    }
        
    QString metadataPath = getMetadataPath();
    qDebug() << "[ArchiveFile] 尝试保存元数据到:" << metadataPath;
    
    // 确保目录存在
    QFileInfo fileInfo(metadataPath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        qDebug() << "[ArchiveFile] 元数据目录不存在，尝试创建:" << dir.path();
        if (!dir.mkpath(".")) {
            qWarning() << "[ArchiveFile] 无法创建元数据目录:" << dir.path();
            return;
        }
    }
    
    QJsonObject metadata;
    metadata["signature"] = signature_;
    metadata["lineCount"] = static_cast<qint64>(line_count_);
    metadata["lastModified"] = QDateTime::currentMSecsSinceEpoch();
    
    QFile metadataFile(metadataPath);
    if(metadataFile.open(QIODevice::WriteOnly)) {
        metadataFile.write(QJsonDocument(metadata).toJson());
        metadataFile.close();
        qDebug() << "[ArchiveFile] 成功保存元数据:" << metadataPath;
    } else {
        qWarning() << "[ArchiveFile] 保存元数据失败:" << metadataPath << "错误:" << metadataFile.errorString();
    }
}

void ArchiveFile::loadMetadata()
{
    if(dir_name_.isEmpty())
        return;
        
    QString metadataPath = getMetadataPath();
    QFile metadataFile(metadataPath);
    
    if(metadataFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(metadataFile.readAll());
        QJsonObject metadata = doc.object();
        
        if(metadata.contains("signature")) {
            signature_ = metadata["signature"].toString();
        }
        if(metadata.contains("lineCount")) {
            line_count_ = static_cast<quint64>(metadata["lineCount"].toVariant().toLongLong());
        }
        
        metadataFile.close();
        qDebug() << "[ArchiveFile] 加载元数据 - 签名:" << signature_ << "行数:" << line_count_;
        
        // 验证元数据中的行数是否与实际文件内容一致
        if (backend_ && backend_->isOpen()) {
            // 临时读取文件内容来计算实际行数
            qint64 currentPos = backend_->pos();
            backend_->seek(0);
            QByteArray content = backend_->readAll();
            backend_->seek(currentPos); // 恢复原位置
            
            // 计算实际行数（按换行符分割）
            QList<QByteArray> lines = content.split('\n');
            quint64 actualLineCount = 0;
            for (const QByteArray& line : lines) {
                if (!line.trimmed().isEmpty()) {
                    actualLineCount++;
                }
            }
            
            // 如果实际行数与元数据中的行数不一致，修正元数据
            if (actualLineCount != line_count_) {
                qDebug() << "[ArchiveFile] 检测到行数不一致，修正元数据 - 元数据:" << line_count_ << "实际:" << actualLineCount;
                line_count_ = actualLineCount;
                // 保存修正后的元数据
                saveMetadata();
            }
        }
    }
}

QString ArchiveFile::getMetadataPath() const
{
    return QString("%1/metadata.json").arg(dir_name_);
}

QString ArchiveFile::getImagesPath() const
{
    return QString("%1/images").arg(dir_name_);
}
