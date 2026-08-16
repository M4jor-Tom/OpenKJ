#include "songshop.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFileInfo>
#include <QDir>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QDebug>


SongShop::SongShop(QObject *parent) : QObject(parent) {
    m_logger = spdlog::get("logger");
    connectionReset = false;
    manager = new QNetworkAccessManager(this);
    connect(manager, &QNetworkAccessManager::sslErrors, this, &SongShop::onSslErrors);
    connect(manager, &QNetworkAccessManager::finished, this, &SongShop::onNetworkReply);
    songsLoaded = false;
    knLoginError = false;
}

void SongShop::updateCache() {
    // return;
    m_logger->info("{} Requesting songs from db.openkj.org", m_loggingPrefix);
    QJsonObject mainObject;
    mainObject.insert("command", "getsongs");
    QJsonDocument jsonDocument;
    jsonDocument.setObject(mainObject);
    QNetworkRequest request(QUrl("https://db.openkj.org/apigetsongs_v2"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    manager->post(request, jsonDocument.toJson());
}

ShopSongs SongShop::getSongs() {
    if (!songsLoaded)
        updateCache();
    return songs;
}

void SongShop::knLogin(QString userName, QString password) {
    knLoginError = false;
    // toLocal8Bit() returns a temporary and password.length() counts UTF-16 code
    // units, not bytes in that encoding, so fromRawData could read past the end
    // of the buffer for non-ASCII passwords. Hash the QByteArray directly.
    const QByteArray passwordBytes = password.toUtf8();
    const QString passHash = QString::fromLatin1(
            QCryptographicHash::hash(passwordBytes, QCryptographicHash::Md5).toHex());

    // Credentials go in the request body, not the query string: a URL is logged
    // by the server, by any intermediary proxy, and by Qt's own network logging.
    QUrlQuery body;
    body.addQueryItem("action", "validate_login");
    body.addQueryItem("username", userName);
    body.addQueryItem("md5", passHash);
    body.addQueryItem("merchant", "99");

    QNetworkRequest request(QUrl("https://www.partytyme.net/songshop/cat/api_account_setup.php"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    manager->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
}

void SongShop::knPurchase(QString songId, QString ccNumber, QString ccM, QString ccY, QString ccCVV) {
    // Cardholder data must never appear in a URL. TLS protects the wire, but a
    // query string lands in the server's access log, in any reverse proxy or CDN
    // in front of it, and in Referer headers — none of which are in scope for
    // cardholder-data protection. A full PAN plus CVV in a log is a direct
    // PCI-DSS violation, so these move into the request body.
    QUrlQuery body;
    body.addQueryItem("media_format", songId.contains("PY") ? "mp3g" : "mp4");
    body.addQueryItem("tracks", songId);
    body.addQueryItem("cc_number", ccNumber);
    body.addQueryItem("cc_cvv", ccCVV);
    body.addQueryItem("cc_exp_mm", ccM);
    body.addQueryItem("cc_exp_yyyy", ccY);
    body.addQueryItem("session_id", knSessionId);
    body.addQueryItem("merchant", "99");

    QNetworkRequest request(QUrl("https://www.partytyme.net/songshop/cat/api_make_order.php"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    manager->post(request, body.toString(QUrl::FullyEncoded).toUtf8());
}

bool SongShop::loggedIn() {
    if (knSessionId != "")
        return true;
    return false;
}

void SongShop::setDlSongInfo(QString artist, QString title, QString songId) {
    dlArtist = artist;
    dlTitle = title;
    dlSongId = songId;
}

void SongShop::downloadFile(const QString &url, const QString &destFn) {
    // Both the URL and the filename originate from the song-shop JSON response,
    // so neither can be trusted.

    // A file:// or http:// URL here would exfiltrate a local file into the
    // download directory, or silently downgrade the transfer.
    const QUrl srcUrl(url);
    if (!srcUrl.isValid() || srcUrl.scheme() != QLatin1String("https")) {
        qWarning() << "songshop: refusing download from non-https URL:" << url;
        emit downloadFailed();
        return;
    }

    QString destDir = m_settings.storeDownloadDir();
    if (!QDir(destDir).exists())
        QDir().mkpath(destDir);

    // fileName() strips any directory component, then the remaining separators
    // and dot segments are removed, so a name like "../../.config/autostart/x"
    // cannot escape the download directory.
    // static: the pattern is constant, so compiling it per call is wasted work
    // and is what clazy's use-static-qregularexpression check asks for.
    static const QRegularExpression illegalChars(QStringLiteral("[/\\\\:*?\"<>|]"));
    QString safeName = QFileInfo(destFn).fileName();
    safeName.remove(illegalChars);
    while (safeName.startsWith('.'))
        safeName.remove(0, 1);
    if (safeName.isEmpty())
        safeName = QStringLiteral("download");

    const QDir dir(destDir);
    const QString destPath = dir.filePath(safeName);

    // Belt and braces: confirm the resolved path really is inside destDir.
    const QString canonicalDir = QDir(destDir).absolutePath();
    if (!QFileInfo(destPath).absoluteFilePath().startsWith(canonicalDir + QDir::separator())) {
        qWarning() << "songshop: refusing to write outside the download directory:" << destPath;
        emit downloadFailed();
        return;
    }

    QNetworkAccessManager m_NetworkMngr;
    QNetworkReply *reply = m_NetworkMngr.get(QNetworkRequest(srcUrl));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::downloadProgress, this, &SongShop::onDownloadProgress);
    loop.exec();
    QUrl aUrl(url);
    QFileInfo fileInfo = aUrl.path();
    QFile file(destPath);
    file.open(QIODevice::WriteOnly);
    file.write(reply->readAll());
    delete reply;
    emit karaokeSongDownloaded(destPath);
    // clear session ID to force login again before next download.  Workaround for expiring PartyTyme logins.
    knSessionId = "";
}

void SongShop::onSslErrors(QNetworkReply *reply, QList<QSslError> errors) {
    reply->abort();
    m_logger->warn("{} Received SSL error when connecting to db.openkj.org, please make sure your system time and date are correct", m_loggingPrefix);
}

void SongShop::onNetworkReply(QNetworkReply *reply) {
    m_logger->trace("{} Received network reply from db.openkj.org", m_loggingPrefix);
    if (reply->error() != QNetworkReply::NoError) {
        m_logger->warn("{} Error connecting to server: {}", m_loggingPrefix, reply->errorString());
        //output some meaningful error msg
        return;
    }
    QByteArray data = reply->readAll();
    QJsonDocument json = QJsonDocument::fromJson(data);
    QString command = json.object().value("command").toString();
    bool error = json.object().value("error").toBool();
    if (error) {
        m_logger->warn("{} Received error reply from server: {}", m_loggingPrefix, json.object().value("errorString").toString());
        return;
    }
    if (command == "getsongs") {
        emit songUpdateStarted();
        QJsonArray songsArray = json.object().value("songs").toArray();
        songs.clear();
        for (int i = 0; i < songsArray.size(); i++) {
            ShopSong song;
            QJsonObject jsonObject = songsArray.at(i).toObject();
            song.artist = jsonObject.value("artist").toString();
            song.title = jsonObject.value("title").toString();
            song.songid = jsonObject.value("songid").toString();
            song.vendor = jsonObject.value("vendor").toString();
            song.price = jsonObject.value("price").toDouble();
            song.type = 0;
            songs.append(song);
        }
        if (!songs.isEmpty())
            songsLoaded = true;
        emit songsUpdated();
    } else if ((json.object().value("result").toString() == "SUCCESS") &&
               (json.object().value("session_id").toString() != "")) {
        knSessionId = json.object().value("session_id").toString();
        knLoginError = false;
        emit knLoginSuccess();
    } else if ((json.object().value("result").toString() == "ERROR") &&
               (json.object().value("error").toString() == "Wrong username or password")) {
        m_logger->warn("PartyTyme login failed, incorrect username or password", m_loggingPrefix);
        knLoginError = true;
        knSessionId = "";
        emit knLoginFailure();
    } else if ((json.object().value("result").toString() == "ERROR") &&
               (json.object().value("error").toString() == "User not logged in")) {
        m_logger->warn("{} PartyTyme reported user not logged in", m_loggingPrefix);
        knSessionId = "";
    } else if ((json.object().value("result").toString() == "SUCCESS") &&
               (json.object().value("download_links").isArray())) {
        m_logger->info("{} Purchase successful", m_loggingPrefix);
        QString url = json.object().value("download_links").toArray().at(0).toString();
        m_logger->info("{} Downloading purchased track", m_loggingPrefix);
        QString fileExt = ".mp4";
        if (url.contains("mp3g"))
            fileExt = ".zip";
        downloadFile(url, QString(dlSongId + " - " + dlArtist + " - " + dlTitle + fileExt));
        m_logger->info("{} Download complete", m_loggingPrefix);

    } else if ((json.object().value("result").toString() == "ERROR") &&
               (json.object().value("error").toString() == "Payment failed. Check your credit card details.")) {
        emit paymentProcessingFailed();
    } else
        m_logger->warn("{} Unexpected JSON data received: {}", m_loggingPrefix, data.toStdString());
}

void SongShop::onDownloadProgress(qint64 received, qint64 total) {
    m_logger->trace("{} Download progress: {} of {} downloaded", m_loggingPrefix, received, total);
    emit downloadProgress(received, total);
}
