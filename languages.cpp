#include "languages.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace {

QVector<LanguageSpec> defaultLanguages() {
    return {
        {"cpp", "C++", {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hxx", ".h"},
         "g++ -g -std=c++17 -Wall -Wextra {file} -o {output}", "{output}", false},

        {"c", "C", {".c"},
         "gcc -g -std=c17 -Wall -Wextra {file} -o {output}", "{output}", false},

        {"java", "Java", {".java"},
         "javac {file}", "java -cp {dir} {class}", false},

        {"python", "Python", {".py", ".pyw"},
         "", "python3 {file}", false},

        {"javascript", "JavaScript", {".js", ".mjs"},
         "", "node {file}", false},

        {"typescript", "TypeScript", {".ts"},
         "", "npx ts-node {file}", false},

        {"go", "Go", {".go"},
         "", "go run {file}", false},

        {"rust", "Rust", {".rs"},
         "rustc {file} -o {output}", "{output}", false},

        {"shell", "Shell", {".sh", ".bash"},
         "", "bash {file}", false},

        {"ruby", "Ruby", {".rb"},
         "", "ruby {file}", false},

        {"php", "PHP", {".php"},
         "", "php {file}", false},
    };
}

} // namespace

LanguageRegistry::LanguageRegistry() : m_specs(defaultLanguages()) {}

QString LanguageRegistry::configPath() {
    QString base = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + "/.config";
    return base + "/tgk/tgkeditor.json";
}

QString LanguageRegistry::outputPathFor(const QString& file) {
    QFileInfo fi(file);
    return fi.absolutePath() + "/" + fi.completeBaseName() + ".bin";
}

QStringList LanguageRegistry::expandArgs(const QString& tmpl,
                                         const QString& file) {
    if (tmpl.trimmed().isEmpty())
        return {};

    QStringList parts = QProcess::splitCommand(tmpl);
    const QFileInfo fi(file);
    const QString absolute = fi.absoluteFilePath();
    const QString dir = fi.absolutePath();
    const QString name = fi.completeBaseName();
    const QString output = outputPathFor(file);

    for (QString& part : parts) {
        part.replace("{file}", absolute);
        part.replace("{dir}", dir);
        part.replace("{name}", name);
        part.replace("{class}", name);
        part.replace("{output}", output);
    }
    return parts;
}

void LanguageRegistry::load() {
    QFile f(configPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        save(); // create the config file on first launch so users can edit it
        return;
    }

    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QJsonObject langs = root["languages"].toObject();
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        QJsonObject o = it.value().toObject();
        LanguageSpec s;
        s.id = it.key();
        s.displayName = o["displayName"].toString(s.id);
        for (const QJsonValue& v : o["extensions"].toArray())
            s.extensions << v.toString();
        s.compile = o["compile"].toString();
        s.run = o["run"].toString();
        s.confirmed = o["confirmed"].toBool(false);

        LanguageSpec* existing = byId(s.id);
        if (existing) {
            *existing = s;
        } else if (!s.extensions.isEmpty()) {
            m_specs.append(s); // user-defined language
        }
    }
}

bool LanguageRegistry::save() const {
    QJsonObject langs;
    for (const LanguageSpec& s : m_specs) {
        QJsonObject o;
        o["displayName"] = s.displayName;
        QJsonArray exts;
        for (const QString& e : s.extensions)
            exts.append(e);
        o["extensions"] = exts;
        o["compile"] = s.compile;
        o["run"] = s.run;
        o["confirmed"] = s.confirmed;
        langs[s.id] = o;
    }

    QJsonObject root;
    root["version"] = 1;
    root["comment"] =
        "tgk editor toolchain config. Placeholders: {file} {dir} {name} "
        "{class} {output}. Delete a language or set confirmed=false to be "
        "asked again. You may add your own languages!";
    root["languages"] = langs;

    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

const LanguageSpec* LanguageRegistry::forFile(const QString& path) const {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
        return nullptr;
    for (const LanguageSpec& s : m_specs)
        for (const QString& e : s.extensions)
            if (e == "." + suffix || e == suffix)
                return &s;
    return nullptr;
}

LanguageSpec* LanguageRegistry::byId(const QString& id) {
    for (LanguageSpec& s : m_specs)
        if (s.id == id)
            return &s;
    return nullptr;
}

void LanguageRegistry::update(const LanguageSpec& spec) {
    if (LanguageSpec* existing = byId(spec.id))
        *existing = spec;
    else
        m_specs.append(spec);
}

void LanguageRegistry::resetDefaults() {
    m_specs = defaultLanguages();
    save();
}
