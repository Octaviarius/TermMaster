#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QXmlStreamWriter>
#include <Termaster/Core/Help/HelpSource.h>
#include <Termaster/Core/Processing/NativeProcessors.h>
#include <Termaster/Core/Processing/ProcessorRegistry.h>
#include <Termaster/Core/SettingsSchema.h>

namespace
{
QString pageFile(const QString& id)
{
    return QString::fromLatin1(QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".html";
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication    application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"manifest", "Source manifest (repeatable).", "path"});
    parser.addOption({"output", "Generated HTML and QHP directory.", "directory"});
    parser.addOption({"namespace", "Unique Qt Help namespace.", "namespace"});
    parser.addOption({"native", "Generate reference from registered native processor descriptors."});
    parser.addOption({"schema", "Append parameter reference: topicID=descriptor.yaml (repeatable).", "binding"});
    parser.process(application);

    const auto output    = parser.value("output");
    const auto nameSpace = parser.value("namespace");
    if (output.isEmpty() || !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,191}$").match(nameSpace).hasMatch()
        || !QDir().mkpath(output))
    {
        qCritical("Invalid help output directory or namespace.");
        return 1;
    }

    HelpSource source;
    QString    error;
    int        owner = 0;
    for (const auto& manifest : parser.values("manifest"))
    {
        if (!source.loadManifest("module" + QString::number(++owner), manifest, &error))
        {
            qCritical().noquote() << manifest << error;
            return 1;
        }
    }
    if (parser.isSet("native"))
    {
        ProcessorRegistry registry;
        registerNativeProcessors(registry, "help.compiler");
        QList<HelpSource::Topic> references;
        for (const auto& type : registry.types())
        {
            HelpSource::Topic topic;
            topic.id       = "processor." + type.id;
            topic.title    = type.title;
            topic.parent   = "processing.nodes";
            topic.markdown = "# " + type.title + "\n\n`" + type.id + "`\n\n" + "Topology: "
                           + QString::number(type.inputCount) + " inputs, " + QString::number(type.outputCount)
                           + " outputs.\n\n" + "Input ports: " + type.inputNames.join(", ")
                           + ".\n\nOutput ports: " + type.outputNames.join(", ") + ".\n\n"
                           + "[Processing concepts](help:processing) · [Lineflow](help:processing.lineflow)\n\n"
                           + HelpSource::schemaReference(type.settingsSchema);
            references.append(std::move(topic));
        }
        if (!source.replaceTopics("native", std::move(references), &error))
        {
            qCritical().noquote() << error;
            return 1;
        }
    }
    QMap<QString, QString> supplements;
    for (const auto& binding : parser.values("schema"))
    {
        const int split = binding.indexOf('=');
        auto      topic = source.topic(binding.left(split));
        QFile     descriptor(binding.mid(split + 1));
        if (split < 1 || !topic || !descriptor.open(QIODevice::ReadOnly) || descriptor.size() > 4 * 1024 * 1024)
        {
            qCritical().noquote() << "Invalid help schema binding:" << binding;
            return 1;
        }
        const auto reference = HelpSource::schemaReference(descriptor.readAll());
        if (reference.isEmpty())
        {
            qCritical().noquote() << "Invalid settings descriptor:" << descriptor.fileName();
            return 1;
        }
        supplements.insert(topic->id, reference);
    }

    QStringList files;
    const auto  assets = source.assets();
    for (auto it = assets.cbegin(); it != assets.cend(); ++it)
    {
        const auto relative = "assets/" + it.key().path().mid(1);
        if (!QDir().mkpath(QFileInfo(QDir(output).filePath(relative)).absolutePath())
            || !writeFile(QDir(output).filePath(relative), it.value()))
        {
            qCritical("Cannot export help image.");
            return 1;
        }
        files.append(relative);
    }
    const auto topics = source.topics();
    for (const auto& topic : topics)
    {
        QTextDocument document;
        document.setResourceProvider([](const QUrl&) -> QVariant { return QByteArray(); });
        document.setBaseUrl(topic.source);
        document.setMarkdown(topic.markdown + "\n\n" + supplements.value(topic.id),
                             QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub)
                                 | QTextDocument::MarkdownNoHTML);
        document.setMetaInformation(QTextDocument::DocumentTitle, topic.title);
        auto                            html = document.toHtml();
        static const QRegularExpression links("(href|src)=\"([^\"]+)\"");
        auto                            matches = links.globalMatch(html);
        QList<QPair<QString, QString>>  replacements;
        while (matches.hasNext())
        {
            const auto match = matches.next();
            const auto url   = topic.source.resolved(
                QUrl::fromEncoded(QTextDocumentFragment::fromHtml(match.captured(2)).toPlainText().toUtf8()));
            const auto target = source.topicForUrl(url);
            if (!target.isEmpty())
            {
                replacements.append({match.captured(0),
                                     match.captured(1) + "=\"" + pageFile(target)
                                         + (url.fragment().isEmpty() ? QString() : "#" + url.fragment().toHtmlEscaped())
                                         + "\""});
            }
            else if (assets.contains(url))
            {
                replacements.append(
                    {match.captured(0), match.captured(1) + "=\"assets/" + url.path().mid(1).toHtmlEscaped() + "\""});
            }
            else if (url.scheme() != "help" && url.scheme() != "https" && url.scheme() != "http"
                     && !match.captured(2).startsWith('#'))
            {
                qCritical().noquote() << "Broken or unsafe help link in" << topic.id << match.captured(2);
                return 1;
            }
        }
        for (const auto& replacement : replacements)
        {
            html.replace(replacement.first, replacement.second);
        }
        const auto file = pageFile(topic.id);
        if (!writeFile(QDir(output).filePath(file), html.toUtf8()))
        {
            qCritical("Cannot write help HTML.");
            return 1;
        }
        files.append(file);
    }

    QByteArray       project;
    QXmlStreamWriter xml(&project);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("QtHelpProject");
    xml.writeAttribute("version", "1.0");
    xml.writeTextElement("namespace", nameSpace);
    xml.writeTextElement("virtualFolder", "doc");
    xml.writeStartElement("filterSection");
    xml.writeStartElement("toc");
    QSet<QString>                                 written;
    std::function<void(const HelpSource::Topic&)> section = [&](const HelpSource::Topic& topic) {
        if (written.contains(topic.id))
        {
            return;
        }
        written.insert(topic.id);
        xml.writeStartElement("section");
        xml.writeAttribute("title", topic.title);
        xml.writeAttribute("ref", pageFile(topic.id));
        for (const auto& child : topics)
        {
            if (child.parent == topic.id)
            {
                section(child);
            }
        }
        xml.writeEndElement();
    };
    for (const auto& topic : topics)
    {
        if (topic.parent.isEmpty() || !source.topic(topic.parent))
        {
            section(topic);
        }
    }
    xml.writeEndElement();
    xml.writeStartElement("keywords");
    for (const auto& topic : topics)
    {
        xml.writeStartElement("keyword");
        xml.writeAttribute("name", topic.title);
        xml.writeAttribute("id", topic.id);
        xml.writeAttribute("ref", pageFile(topic.id));
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeStartElement("files");
    for (const auto& file : files)
    {
        xml.writeTextElement("file", file);
    }
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return writeFile(QDir(output).filePath("help.qhp"), project) ? 0 : 1;
}
