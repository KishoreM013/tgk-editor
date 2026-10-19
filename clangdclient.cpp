#include "clangdclient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <cstdio>

namespace {

// clangd may still send snippets ($1, ${2:foo}); strip placeholders.
QString cleanSnippet(QString s) {
    static const QRegularExpression re("\\$\\{\\d+:([^}]*)\\}|\\$\\d+");
    QString out;
    int last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += m.captured(1);
        last = m.capturedEnd();
    }
    out += s.mid(last);
    return out;
}

QString uriToPath(const QString& uri) {
    return QUrl(uri).toLocalFile();
}

} // namespace

ClangdClient::ClangdClient(QObject* parent) : QObject(parent) {}

ClangdClient::~ClangdClient() {
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
}

bool ClangdClient::start(const QString& rootDir) {
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        return true;

    m_initialized = false;
    m_expected = 0;
    m_buf.clear();
    m_rootUri = QUrl::fromLocalFile(rootDir).toString();

    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] {
            m_buf += m_proc->readAllStandardOutput();
            processBuffer();
        });
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this] {
                    m_initialized = false;
                    m_pending.clear();
                    m_queue.clear();
                    emit serverDied();
                });
    }

    m_proc->start("clangd",
                  {"--background-index=false", "--clang-tidy=false",
                   "--log=error", "-j=2"});
    if (!m_proc->waitForStarted(3000))
        return false;

    QJsonObject caps;
    QJsonObject compItem;
    compItem["snippetSupport"] = false;
    QJsonObject completion;
    completion["completionItem"] = compItem;
    QJsonObject td;
    td["completion"] = completion;
    caps["textDocument"] = td;

    QJsonObject params;
    params["processId"] = QJsonValue(qint64(QCoreApplication::applicationPid()));
    params["rootUri"] = m_rootUri;
    params["capabilities"] = caps;

    m_initId = m_nextId++;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = m_initId;
    msg["method"] = "initialize";
    msg["params"] = params;
    sendRaw(msg);
    return true;
}

bool ClangdClient::isRunning() const {
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void ClangdClient::openDocument(const QString& path, const QString& text) {
    enqueue([this, path, text] {
        QJsonObject td;
        td["uri"] = QUrl::fromLocalFile(path).toString();
        td["languageId"] = "cpp";
        td["version"] = 1;
        td["text"] = text;
        m_versions[path] = 1;
        QJsonObject params;
        params["textDocument"] = td;
        sendNotification("textDocument/didOpen", params);
    });
}

void ClangdClient::changeDocument(const QString& path, const QString& text) {
    enqueue([this, path, text] {
        int ver = m_versions.value(path, 0) + 1;
        m_versions[path] = ver;
        QJsonObject td;
        td["uri"] = QUrl::fromLocalFile(path).toString();
        td["version"] = ver;
        td["text"] = text;
        QJsonObject params;
        params["textDocument"] = td;
        sendNotification("textDocument/didChange", params);
    });
}

void ClangdClient::closeDocument(const QString& path) {
    enqueue([this, path] {
        QJsonObject td;
        td["uri"] = QUrl::fromLocalFile(path).toString();
        QJsonObject params;
        params["textDocument"] = td;
        sendNotification("textDocument/didClose", params);
        m_versions.remove(path);
    });
}

void ClangdClient::completion(const QString& path, int line, int character,
                              CompletionCallback cb) {
    if (!isRunning()) {
        cb(false, {});
        return;
    }
    int id = m_nextId++;
    enqueue([this, id, path, line, character] {
        QJsonObject td;
        td["uri"] = QUrl::fromLocalFile(path).toString();
        QJsonObject pos;
        pos["line"] = line;
        pos["character"] = character;
        QJsonObject ctx;
        ctx["triggerKind"] = 1; // invoked
        QJsonObject params;
        params["textDocument"] = td;
        params["position"] = pos;
        params["context"] = ctx;
        QJsonObject msg;
        msg["jsonrpc"] = "2.0";
        msg["id"] = id;
        msg["method"] = "textDocument/completion";
        msg["params"] = params;
        sendRaw(msg);
    });
    m_pending.append({id, std::move(cb)});
}

void ClangdClient::enqueue(std::function<void()> fn) {
    if (m_initialized)
        fn();
    else
        m_queue.append(std::move(fn));
}

void ClangdClient::flushQueue() {
    auto queue = std::move(m_queue);
    m_queue.clear();
    for (auto& fn : queue)
        fn();
}

void ClangdClient::sendRaw(const QJsonObject& msg) {
    if (!m_proc || m_proc->state() == QProcess::NotRunning)
        return;
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    if (qEnvironmentVariableIsSet("CPPIDE_TRACE"))
        fprintf(stderr, "LTS: %s\n", body.left(160).constData());
    QByteArray frame = "Content-Length: " + QByteArray::number(body.size()) +
                       "\r\n\r\n" + body;
    m_proc->write(frame);
}

void ClangdClient::sendRequest(const QString& method,
                               const QJsonObject& params) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = m_nextId++;
    msg["method"] = method;
    msg["params"] = params;
    sendRaw(msg);
}

void ClangdClient::sendNotification(const QString& method,
                                    const QJsonObject& params) {
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = method;
    msg["params"] = params;
    sendRaw(msg);
}

void ClangdClient::processBuffer() {
    for (;;) {
        if (m_expected == 0) {
            int idx = m_buf.indexOf("\r\n\r\n");
            if (idx < 0)
                return;
            QByteArray header = m_buf.left(idx);
            m_buf.remove(0, idx + 4);
            static const QRegularExpression lenRe(
                "Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
            auto m = lenRe.match(QString::fromLatin1(header));
            if (!m.hasMatch())
                continue;
            m_expected = m.captured(1).toLongLong();
        }
        if (m_buf.size() < m_expected)
            return;
        QByteArray payload = m_buf.left(m_expected);
        m_buf.remove(0, m_expected);
        m_expected = 0;
        if (qEnvironmentVariableIsSet("CPPIDE_TRACE"))
            fprintf(stderr, "LTP: %s\n", payload.left(160).constData());
        handlePayload(payload);
    }
}

void ClangdClient::handlePayload(const QByteArray& json) {
    QJsonObject obj = QJsonDocument::fromJson(json).object();
    if (obj.isEmpty())
        return;

    // Response to one of our requests
    if (obj.contains("id") && (obj.contains("result") || obj.contains("error"))) {
        int id = obj["id"].toInt();

        if (id == m_initId) {
            if (!obj.contains("error")) {
                sendNotification("initialized", QJsonObject{});
                m_initialized = true;
                flushQueue();
            }
            return;
        }

        for (int i = 0; i < m_pending.size(); ++i) {
            if (m_pending[i].id != id)
                continue;
            CompletionCallback cb = std::move(m_pending[i].cb);
            m_pending.remove(i);

            QVector<ClangdItem> items;
            QJsonValue result = obj["result"];
            QJsonArray arr;
            if (result.isArray())
                arr = result.toArray();
            else if (result.isObject())
                arr = result.toObject()["items"].toArray();

            for (const QJsonValue& v : arr) {
                QJsonObject o = v.toObject();
                ClangdItem it;
                it.label = o["label"].toString();
                it.detail = o["detail"].toString();
                it.kind = o["kind"].toInt();
                QString ins = o["insertText"].toString();
                QJsonObject te = o["textEdit"].toObject();
                if (!te.isEmpty()) {
                    QJsonObject r = te["range"].toObject();
                    QJsonObject s = r["start"].toObject();
                    QJsonObject e = r["end"].toObject();
                    it.hasRange = true;
                    it.startLine = s["line"].toInt();
                    it.startCol = s["character"].toInt();
                    it.endLine = e["line"].toInt();
                    it.endCol = e["character"].toInt();
                    ins = te["newText"].toString();
                }
                it.insertText = cleanSnippet(ins.isEmpty() ? it.label : ins);
                if (!it.insertText.isEmpty())
                    items.append(it);
            }
            cb(true, items);
            return;
        }
        return;
    }

    // Server -> client notification
    const QString method = obj["method"].toString();
    if (method == "textDocument/publishDiagnostics") {
        QJsonObject params = obj["params"].toObject();
        QString path = uriToPath(params["uri"].toString());
        QVector<ClangdDiagnostic> diags;
        for (const QJsonValue& v : params["diagnostics"].toArray()) {
            QJsonObject o = v.toObject();
            ClangdDiagnostic d;
            d.message = o["message"].toString();
            d.severity = o["severity"].toInt(1);
            QJsonObject s = o["range"].toObject()["start"].toObject();
            d.line = s["line"].toInt();
            d.col = s["character"].toInt();
            diags.append(d);
        }
        emit diagnostics(path, diags);
    }
}
