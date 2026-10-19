#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// One language: how to compile and run files of this kind.
// Commands are templates that may use these placeholders:
//   {file}   absolute path of the source file
//   {dir}    directory containing the source file
//   {name}   file name without extension
//   {class}  same as {name} (convenience for Java)
//   {output} build artifact path (e.g. /path/prog.bin)
struct LanguageSpec {
    QString id;           // "cpp"
    QString displayName;  // "C++"
    QStringList extensions;
    QString compile;      // empty => interpreted (no compile step)
    QString run;
    bool confirmed = false; // user already approved the commands
};

// Loads built-in defaults, merges the user's JSON config and persists
// changes. Config lives in ~/.config/tgk/tgkeditor.json.
class LanguageRegistry {
public:
    LanguageRegistry();

    void load();
    bool save() const;

    static QString configPath();
    static QString outputPathFor(const QString& file);
    static QString expand(const QString& tmpl, const QString& file);

    const LanguageSpec* forFile(const QString& path) const;
    LanguageSpec* byId(const QString& id);
    QVector<LanguageSpec> all() const { return m_specs; }
    void update(const LanguageSpec& spec); // replace by id (or add)
    void resetDefaults();

private:
    QVector<LanguageSpec> m_specs;
};
