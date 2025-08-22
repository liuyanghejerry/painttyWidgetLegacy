#ifndef KNOWN_ERROR_H
#define KNOWN_ERROR_H

#include <QString>
#include <QJsonObject>
#include <tuple>

/**
 * @brief 将错误码转换为对应的错误消息
 * @param errorCode 错误码
 * @return 对应的错误消息字符串
 */
QString getKnownErrorMessage(int errorCode);

/**
 * @brief 检查响应是否为错误，并返回错误消息
 * @param response JSON响应对象
 * @return std::tuple<bool, QString> 第一个元素表示是否为错误，第二个元素为错误消息
 */
std::tuple<bool, QString> getErrorMessageIfError(const QJsonObject& response);

#endif // KNOWN_ERROR_H 