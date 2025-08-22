#include <QJsonObject>
#include <QObject>
#include <QString>

QString getKnownErrorMessage(int errorCode) {
  switch (errorCode) {
    // General errors (200-299)
    case 200: return QObject::tr("Unknown error");
    case 201: return QObject::tr("Server busy");
    case 202: return QObject::tr("Room name already exists");
    case 203: return QObject::tr("Invalid name");
    case 204: return QObject::tr("Invalid maximum member count");
    case 205: return QObject::tr("Invalid welcome message");
    case 206: return QObject::tr("Invalid empty room close setting");
    case 207: return QObject::tr("Invalid password");
    case 208: return QObject::tr("Auto-close empty rooms not supported");
    case 209: return QObject::tr("Private rooms not supported");
    case 210: return QObject::tr("Room limit reached");
    case 211: return QObject::tr("Invalid canvas size");

    // Login errors (300-399)
    case 300: return QObject::tr("Unknown error during login");
    case 301: return QObject::tr("Invalid username");
    case 302: return QObject::tr("Invalid password or password missing");
    case 303: return QObject::tr("Room is full");
    case 304: return QObject::tr("You have been banned");
    case 305: return QObject::tr("Server busy");

    // Archive errors (800-899)
    case 800: return QObject::tr("Unknown error during archive operation");

    // Online list errors (600-699)
    case 600: return QObject::tr("Unknown error getting online list");
    case 601: return QObject::tr("Room is closed");

    // Check-in room errors (700-799)
    case 700: return QObject::tr("Unknown error during check-in");
    case 701: return QObject::tr("Invalid key provided during check-in");
    case 702: return QObject::tr("Check-in too late");

    default: return QObject::tr("Unknown error code: %1").arg(errorCode);
  }
}

// tuple: <isError, errorMessage>
std::tuple<bool, QString> getErrorMessageIfError(const QJsonObject& response) {
  bool isError = !response["result"].toBool(false);
  if (!isError) {
    return std::make_tuple(false, QString());
  }
  int code = response["errcode"].toInt(0);
  if (code != 0) {
    return std::make_tuple(true, getKnownErrorMessage(code)); 
  }
  return std::make_tuple(true, QObject::tr("Unknown error"));
}