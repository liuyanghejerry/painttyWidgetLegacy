#ifndef ARCHIVEFILE_H
#define ARCHIVEFILE_H

#include <QObject>
#include <QJsonObject>
#include <QImage>
class QFile;

class ArchiveFile : public QObject
{
    Q_OBJECT
public:
    explicit ArchiveFile(const QString &name,
                         const QString &signature,
                         QObject *parent = 0);
    explicit ArchiveFile(QObject *parent = 0);
    ~ArchiveFile();
    
    // 基本文件操作
    QByteArray readAll() const;
    QList<QByteArray> readAllLines() const; // 新增：逐行读取所有数据
    quint64 lineCount() const;
    quint64 size() const;
    QString name() const;
    QString signature() const;
    QString dirName() const;
    
    // 新增：行数管理
    void setLineCount(quint64 count);
    
    // 新增：数据同步状态管理
    bool isDataConsistent() const;
    bool isSignatureValid() const;
    bool isLineCountValid() const;
    
    // 数据同步方法
    void resetForNewSignature(const QString &newSignature);
    void appendArchiveData(const QByteArray &data);
    
    // 新增：PNG缓存管理
    bool saveSnapshotImages(const QList<QImage> &layerImages);
    QList<QImage> loadSnapshotImages() const;
    bool hasCachedImages() const;
    void clearCachedImages() const;
    
signals:
    void newSignature(const QString&);
    void lineCountChanged(quint64 newCount);
    
public slots:
    void setName(const QString &name);
    void appendData(const QByteArray&);
    void setSignature(const QString& sign);
    void flush();
    void prune();
    void remove();
    
protected:
    QString signature_;
    QString name_;
    QString dir_name_;
    QFile* backend_;
    quint64 line_count_; // 新增：行数计数
    
private:
    Q_DISABLE_COPY(ArchiveFile)
    bool createFile();
    void saveMetadata();
    void loadMetadata();
    QString getMetadataPath() const;
    QString getImagesPath() const;
};

#endif // ARCHIVEFILE_H
