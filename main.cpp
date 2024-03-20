/*****************************************************************

Copyright (C) 2009 Lubos Lunak <l.lunak@suse.cz>
Copyright (C) 2017 Fabian Vogt <fabian@ritter-vogt.de>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#include "main.h"

#include <cassert>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

#include <QWindow>
#include <QtCore/QCommandLineParser>
#include <QtCore/QHash>
#include <QtCore/QMimeDatabase>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>

#include <KConfigCore/KConfigGroup>
#include <KConfigCore/KSharedConfig>
#include <KCoreAddons/KAboutData>
#include <KCoreAddons/KProcess>
#include <KCoreAddons/KShell>
#include <KI18n/KLocalizedString>
#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>
#include <KService/KApplicationTrader>
#include <kio_version.h>
#if KIO_VERSION >= QT_VERSION_CHECK(5, 98, 0)
#include <KIO/JobUiDelegateFactory>
#else
#include <KIO/JobUiDelegate>
#endif
#include <KIO/OpenUrlJob>
#include <KIOCore/KRecentDocument>
#include <KIOWidgets/KOpenWithDialog>
#include <KNotifications/KNotification>
#include <KProtocolInfo>
#include <KService/KMimeTypeTrader>
#include <KWindowSystem/KWindowSystem>

//#define DEBUG_KDE

#define HELPER_VERSION 6
#define APP_HELPER_VERSION "5.0.6"

int main(int argc, char *argv[])
{
    // Avoid getting started by the session manager
    qunsetenv("SESSION_MANAGER");

    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);

    QApplication app(argc, argv);

    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);

    // Check whether we're called from Firefox or Thunderbird
    QString appname = i18n("Mozilla Firefox");
    QString parent = QFile::symLinkTarget(QString::fromUtf8("/proc/%1/exe").arg(int(getppid())));
    if (parent.contains(QString::fromUtf8("thunderbird"), Qt::CaseInsensitive))
        appname = i18n("Mozilla Thunderbird");

    // This shows on file dialogs
    KAboutData about(QString::fromUtf8("kmozillahelper"), appname, QString::fromUtf8(APP_HELPER_VERSION));
    about.setBugAddress("https://bugzilla.opensuse.org/enter_bug.cgi");
    KAboutData::setApplicationData(about);
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);

    app.setQuitOnLastWindowClosed(false);

    Helper helper;

    app.installEventFilter(&helper);

    return app.exec();
}

Helper::Helper() : notifier(STDIN_FILENO, QSocketNotifier::Read), arguments_read(false)
{
    connect(&notifier, &QSocketNotifier::activated, this, &Helper::readCommand);
}

static bool runApplication(const KService::Ptr &service, const QList<QUrl> &urls)
{
    auto *job = new KIO::ApplicationLauncherJob(service);
    job->setUrls(urls);
#if KIO_VERSION >= QT_VERSION_CHECK(5, 98, 0)
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#else
    job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#endif
    job->start();
    return true;
}

static bool openUrl(QUrl url, QString *mime)
{
    auto *job = new KIO::OpenUrlJob(url, *mime);
#if KIO_VERSION >= QT_VERSION_CHECK(5, 98, 0)
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#else
    job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#endif
    job->start();
    return true;
}

void Helper::readCommand()
{
    QString command = readLine();
    if (!std::cin.good())
    {
#ifdef DEBUG_KDE
        std::cerr << "EOF, exiting." << std::endl;
#endif
        QCoreApplication::exit();
        return;
    }

    /* Allow multiple commands at once.
       Firefox nests the event loop in the same way we do,
       so if a file dialog is open, another command may arrive which we handle
       in our nested event loop...
    // For now we only allow one command at once.
    // We need to do this as dialogs spawn their own eventloop and thus they get nested...
    notifier.setEnabled(false); */

#ifdef DEBUG_KDE
    std::cerr << "COMMAND: " << command.toStdString() << std::endl;
#endif
    bool status;
    if (command == QString::fromUtf8("CHECK"))
        status = handleCheck();
    else if (command == QString::fromUtf8("GETPROXY"))
        status = handleGetProxy();
    else if (command == QString::fromUtf8("HANDLEREXISTS"))
        status = handleHandlerExists();
    else if (command == QString::fromUtf8("GETFROMEXTENSION"))
        status = handleGetFromExtension();
    else if (command == QString::fromUtf8("GETFROMTYPE"))
        status = handleGetFromType();
    else if (command == QString::fromUtf8("GETAPPDESCFORSCHEME"))
        status = handleGetAppDescForScheme();
    else if (command == QString::fromUtf8("APPSDIALOG"))
        status = handleAppsDialog();
    else if (command == QString::fromUtf8("GETOPENFILENAME"))
        status = handleGetOpenOrSaveX(false, false);
    else if (command == QString::fromUtf8("GETOPENURL"))
        status = handleGetOpenOrSaveX(true, false);
    else if (command == QString::fromUtf8("GETSAVEFILENAME"))
        status = handleGetOpenOrSaveX(false, true);
    else if (command == QString::fromUtf8("GETSAVEURL"))
        status = handleGetOpenOrSaveX(true, true);
    else if (command == QString::fromUtf8("GETDIRECTORYFILENAME"))
        status = handleGetDirectoryX(false);
    else if (command == QString::fromUtf8("GETDIRECTORYURL"))
        status = handleGetDirectoryX(true);
    else if (command == QString::fromUtf8("OPEN"))
        status = handleOpen();
    else if (command == QString::fromUtf8("REVEAL"))
        status = handleReveal();
    else if (command == QString::fromUtf8("RUN"))
        status = handleRun();
    else if (command == QString::fromUtf8("GETDEFAULTFEEDREADER"))
        status = handleGetDefaultFeedReader();
    else if (command == QString::fromUtf8("OPENMAIL"))
        status = handleOpenMail();
    else if (command == QString::fromUtf8("OPENNEWS"))
        status = handleOpenNews();
    else if (command == QString::fromUtf8("ISDEFAULTBROWSER"))
        status = handleIsDefaultBrowser();
    else if (command == QString::fromUtf8("SETDEFAULTBROWSER"))
        status = handleSetDefaultBrowser();
    else if (command == QString::fromUtf8("DOWNLOADFINISHED"))
        status = handleDownloadFinished();
    else
    {
        std::cerr << "Unknown command for KDE helper: " << command.toStdString() << std::endl;
        status = false;
    }
    // status done as \1 (==ok) and \0 (==not ok), because otherwise this cannot happen
    // in normal data (\ is escaped otherwise)
    outputLine(status ? QString::fromUtf8("\\1") : QString::fromUtf8("\\0"), false); // do not escape

    /* See comment on setEnabled above
    notifier.setEnabled(true); */
}

bool Helper::handleCheck()
{
    if (!readArguments(1))
        return false;
    int version = getArgument().toInt(); // requested version
    if (!allArgumentsUsed())
        return false;
    if (version <= HELPER_VERSION) // we must have the exact requested version
        return true;
    std::cerr << "KDE helper version too old." << std::endl;
    return false;
}

bool Helper::handleGetProxy()
{
    // expect to be fed with argument: [protocol://]domain.tld:port
    if (!readArguments(1))
        return false;
    QUrl url = QUrl::fromUserInput(getArgument());
    if (!allArgumentsUsed())
        return false;

    /* get the url elements
       if only the doamin.tld is given, aka. without protocol, this method returns http by default */
    QString proxy = url.scheme();

    /* if the url is valid, we check if it's a proxy
       return DIRECT otherwise */
    if (url.isValid())
    {
        if (proxy == QString::fromUtf8("PROXY") || proxy == QString::fromUtf8("SOCKS5"))
        {
            /* ref. https://developer.mozilla.org/en-US/docs/Web/HTTP/Proxy_servers_and_tunneling/Proxy_Auto-Configuration_PAC_file
            /  format: "PROXY hostname:port" */
            outputLine(proxy.toUpper() + QString::fromUtf8(" ") + url.host() + QString::fromUtf8(":") +
                       QString::number(url.port()));
        }
        else
        {
            // we don't deal with other protocols
            outputLine(QString::fromUtf8("DIRECT"));
        }
        return true;
    }
    return false;
}

bool Helper::handleHandlerExists()
{
    // Cache protocols types to avoid causing Thunderbird to hang (https://bugzilla.suse.com/show_bug.cgi?id=1037806).
    static QHash<QString, bool> known_protocols;

    if (!readArguments(1))
        return false;
    QString protocol = getArgument();
    if (!allArgumentsUsed())
        return false;

    auto it(known_protocols.find(protocol));
    if (it == known_protocols.end())
        it = known_protocols.insert(protocol, KProtocolInfo::isHelperProtocol(protocol));

    if (*it)
        return true;

    return KApplicationTrader::preferredService(QLatin1String("x-scheme-handler/") + protocol) != nullptr;
}

bool Helper::handleGetFromExtension()
{
    if (!readArguments(1))
        return false;
    QString ext = getArgument();
    if (!allArgumentsUsed())
        return false;
    if (!ext.isEmpty())
    {
        QList<QMimeType> mimeList = QMimeDatabase().mimeTypesForFileName(QString::fromUtf8("foo.") + ext);
        for (const QMimeType &mime : mimeList)
            if (mime.isValid())
                return writeMimeInfo(mime);
    }
    return false;
}

bool Helper::handleGetFromType()
{
    if (!readArguments(1))
        return false;
    QString type = getArgument();
    if (!allArgumentsUsed())
        return false;
    QMimeType mime = QMimeDatabase().mimeTypeForName(type);
    if (mime.isValid())
        return writeMimeInfo(mime);
    // firefox also asks for protocol handlers using getfromtype
    QString app = getAppForProtocol(type);
    if (!app.isEmpty())
    {
        outputLine(type);
        outputLine(type); // TODO probably no way to find a good description
        outputLine(app);
        return true;
    }
    return false;
}

bool Helper::writeMimeInfo(QMimeType mime)
{
    KService::Ptr service = KApplicationTrader::preferredService(mime.name());
    if (service)
    {
        outputLine(mime.name());
        outputLine(mime.comment());
        outputLine(service->name());
        return true;
    }
    return false;
}

bool Helper::handleGetAppDescForScheme()
{
    if (!readArguments(1))
        return false;
    QString scheme = getArgument();
    if (!allArgumentsUsed())
        return false;
    QString app = getAppForProtocol(scheme);
    if (!app.isEmpty())
    {
        outputLine(app);
        return true;
    }
    return false;
}

bool Helper::handleAppsDialog()
{
    if (!readArguments(1))
        return false;
    QString title = getArgument();
    long wid = getArgumentParent();
    if (!allArgumentsUsed())
        return false;
    KOpenWithDialog dialog(nullptr);
    if (!title.isEmpty())
        dialog.setWindowTitle(title);
    dialog.hideNoCloseOnExit();
    dialog.hideRunInTerminal(); // TODO
    if (wid != 0)
    {
        dialog.setAttribute(Qt::WA_NativeWindow, true);
        QWindow *subWindow = dialog.windowHandle();
        if (subWindow)
            KWindowSystem::setMainWindow(subWindow, wid);
    }
    if (dialog.exec())
    {
        KService::Ptr service = dialog.service();
        QString command;
        if (service)
            command = service->exec();
        else if (!dialog.text().isEmpty())
            command = dialog.text();
        else
            return false;
        command = command.split(QString::fromUtf8(" ")).first(); // only the actual command
        command = QStandardPaths::findExecutable(command);
        if (command.isEmpty())
            return false;
        outputLine(QUrl::fromUserInput(command).url());
        return true;
    }
    return false;
}

QStringList Helper::convertToNameFilters(const QString &input)
{
    QStringList ret;

    // Filters separated by newline
    for (auto &filter : input.split(QLatin1Char('\n')))
    {
        // Filer exp and name separated by '|'.
        // TODO: Is it possible that | appears in either of those?
        auto data = filter.split(QLatin1Char('|'));

        if (data.length() == 1)
            ret.append(QStringLiteral("%0 Files(%0)").arg(data[0]));
        else if (data.length() >= 2)
            ret.append(QStringLiteral("%0 (%1)(%1)").arg(data[1], data[0]));
    }

    return ret;
}

bool Helper::handleGetOpenOrSaveX(bool url, bool save)
{
    if (!readArguments(4))
        return false;
    QUrl defaultPath = QUrl::fromLocalFile(getArgument());
    // Use dialog.nameFilters() instead of filtersParsed as setNameFilters does some syntax changes
    QStringList filtersParsed = convertToNameFilters(getArgument());
    int selectFilter = getArgument().toInt();
    QString title = getArgument();
    bool multiple = save ? false : isArgument(QString::fromUtf8("MULTIPLE"));
    this->wid = getArgumentParent();
    if (!allArgumentsUsed())
        return false;

    if (title.isEmpty())
        title = save ? i18n("Save") : i18n("Open");

    QFileDialog dialog(nullptr, title, defaultPath.path());

    dialog.selectFile(defaultPath.fileName());
    dialog.setNameFilters(filtersParsed);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, false);
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);

    if (save)
        dialog.setFileMode((QFileDialog::AnyFile));
    else
        dialog.setFileMode(multiple ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);

    if (selectFilter >= 0 && selectFilter >= dialog.nameFilters().size())
        dialog.selectNameFilter(dialog.nameFilters().at(selectFilter));

        // If url == false only allow local files. Impossible to do with Qt < 5.6...
#if (QT_VERSION >= QT_VERSION_CHECK(5, 6, 0))
    if (url == false)
        dialog.setSupportedSchemes(QStringList(QStringLiteral("file")));
#endif

    // Run dialog
    if (dialog.exec() != QDialog::Accepted)
        return false;

    int usedFilter = dialog.nameFilters().indexOf(dialog.selectedNameFilter());

    if (url)
    {
        QList<QUrl> result = dialog.selectedUrls();
        result.removeAll(QUrl());
        if (!result.isEmpty())
        {
            outputLine(QStringLiteral("%0").arg(usedFilter));
            for (const QUrl &url : result)
                outputLine(url.url());
            return true;
        }
    }
    else
    {
        QStringList result = dialog.selectedFiles();
        result.removeAll(QString());
        if (!result.isEmpty())
        {
            outputLine(QStringLiteral("%0").arg(usedFilter));
            for (const QString &str : result)
                outputLine(str);
            return true;
        }
    }
    return false;
}

bool Helper::handleGetDirectoryX(bool url)
{
    if (!readArguments(2))
        return false;
    QString startDir = getArgument();
    QString title = getArgument();
    this->wid = getArgumentParent();
    if (!allArgumentsUsed())
        return false;

    if (url)
    {
        QUrl result = QFileDialog::getExistingDirectoryUrl(nullptr, title, QUrl::fromLocalFile(startDir));
        if (result.isValid())
        {
            outputLine(result.url());
            return true;
        }
    }
    else
    {
        QString result = QFileDialog::getExistingDirectory(nullptr, title, startDir);
        if (!result.isEmpty())
        {
            outputLine(result);
            return true;
        }
    }
    return false;
}

bool Helper::handleOpen()
{
    if (!readArguments(1))
        return false;
    QUrl url = QUrl::fromUserInput(getArgument());
    QString mime;
    if (isArgument(QString::fromUtf8("MIMETYPE")))
        mime = getArgument();
    if (!allArgumentsUsed())
        return false;

    return openUrl(url, &mime);
}

bool Helper::handleReveal()
{
    if (!readArguments(1))
        return false;
    QString path = getArgument();
    if (!allArgumentsUsed())
        return false;
    const KService::List apps = KApplicationTrader::queryByMimeType(QString::fromUtf8("inode/directory"));
    if (apps.size() != 0)
    {
        QString command = apps.at(0)->exec().split(QString::fromLatin1(" ")).first(); // only the actual command
        if (command == QString::fromUtf8("dolphin") || command == QString::fromUtf8("konqueror"))
        {
            command = QStandardPaths::findExecutable(command);
            if (command.isEmpty())
            {
                return false;
            }
            return KProcess::startDetached(command, QStringList() << QString::fromUtf8("--select") << path);
        }
    }
    QFileInfo info(path);
    QString dir = info.dir().path();
    return openUrl(QUrl::fromLocalFile(dir), nullptr);
}

bool Helper::handleRun()
{
    if (!readArguments(2))
        return false;
    QString app = getArgument();
    QString arg = getArgument();
    if (!allArgumentsUsed())
        return false;
    auto job = new KIO::CommandLauncherJob(KShell::quoteArg(app), {KShell::quoteArg(arg)});
#if KIO_VERSION >= QT_VERSION_CHECK(5, 98, 0)
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#else
    job->setUiDelegate(new KIO::JobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, nullptr));
#endif
    job->start();
    return true;
}

bool Helper::handleGetDefaultFeedReader()
{
    if (!readArguments(0))
        return false;
    // firefox wants the full path
    QString reader =
        QStandardPaths::findExecutable(QString::fromUtf8("akregator")); // TODO there is no KDE setting for this
    if (!reader.isEmpty())
    {
        outputLine(reader);
        return true;
    }
    return false;
}

bool Helper::handleOpenMail()
{
    if (!readArguments(0))
        return false;
    // this is based on ktoolinvocation_x11.cpp, there is no API for this
    KConfig config(QString::fromUtf8("emaildefaults"));
    QString groupname = KConfigGroup(&config, "Defaults").readEntry("Profile", "Default");
    KConfigGroup group(&config, QString::fromStdString("PROFILE_%1").arg(groupname));
    QString command = group.readPathEntry("EmailClient", QString());
    if (command.isEmpty())
        command = QString::fromUtf8("kmail");
    if (group.readEntry("TerminalClient", false))
    {
        QString terminal = KConfigGroup(KSharedConfig::openConfig(), "General")
                               .readPathEntry("TerminalApplication", QString::fromUtf8("konsole"));
        command = terminal + QString::fromUtf8(" -e ") + command;
    }
    KService::Ptr mail = KService::serviceByDesktopName(command.split(QLatin1Char(' ')).first());
    if (mail)
    {
        return runApplication(mail, QList<QUrl>()); // TODO parent
    }
    return false;
}

bool Helper::handleOpenNews()
{
    if (!readArguments(0))
        return false;
    KService::Ptr news =
        KService::serviceByDesktopName(QString::fromUtf8("knode")); // TODO there is no KDE setting for this
    if (news)
    {
        //KApplication::updateUserTimestamp(0); // TODO
        return runApplication(news, QList<QUrl>()); // TODO parent
    }
    return false;
}

bool Helper::handleIsDefaultBrowser()
{
    if (!readArguments(0))
        return false;
    QString browser = KConfigGroup(KSharedConfig::openConfig(QString::fromUtf8("kdeglobals")), "General")
                          .readEntry("BrowserApplication");
    return browser == QString::fromUtf8("MozillaFirefox") || browser == QString::fromUtf8("MozillaFirefox.desktop") ||
           browser == QString::fromUtf8("!firefox") || browser == QString::fromUtf8("!/usr/bin/firefox") ||
           browser == QString::fromUtf8("firefox") || browser == QString::fromUtf8("firefox.desktop");
}

bool Helper::handleSetDefaultBrowser()
{
    if (!readArguments(1))
        return false;
    bool alltypes = (getArgument() == QString::fromUtf8("ALLTYPES"));
    if (!allArgumentsUsed())
        return false;
    KConfigGroup(KSharedConfig::openConfig(QString::fromUtf8("kdeglobals")), "General")
        .writeEntry("BrowserApplication", "firefox");
    if (alltypes)
    {
        // TODO there is no API for this and it is a bit complex
    }
    return true;
}

bool Helper::handleDownloadFinished()
{
    if (!readArguments(1))
        return false;
    QString download = getArgument();
    if (!allArgumentsUsed())
        return false;
    // TODO cheat a bit due to i18n freeze - the strings are in the .notifyrc file,
    // taken from KGet, but the notification itself needs the text too.
    // So create it from there.
    KConfig cfg(QString::fromUtf8("kmozillahelper.notifyrc"), KConfig::FullConfig, QStandardPaths::AppDataLocation);
    QString message = KConfigGroup(&cfg, "Event/downloadfinished").readEntry("Comment");
    KNotification::event(QString::fromUtf8("downloadfinished"), download + QString::fromUtf8(" : ") + message);
    return true;
}

QString Helper::getAppForProtocol(const QString &protocol)
{
    /* Inspired by kio's krun.cpp */
    const KService::Ptr service = KApplicationTrader::preferredService(QLatin1String("x-scheme-handler/") + protocol);
    if (service)
        return service->name();

    /* Some KDE services (e.g. vnc) also support application associations.
     * Those are known as "Helper Protocols".
     * However, those aren't also registered using fake mime types and there
     * is no link to a .desktop file...
     * So we need to query for the service to use and then find the .desktop
     * file for that application by comparing the Exec values. */

    if (!KProtocolInfo::isHelperProtocol(protocol))
        return {};

    QString exec = KProtocolInfo::exec(protocol);

    if (exec.isEmpty())
        return {};

    if (exec.contains(QLatin1Char(' ')))
        exec = exec.split(QLatin1Char(' ')).first(); // first part of command

    if (KService::Ptr service = KService::serviceByDesktopName(exec))
        return service->name();

    QString servicename;
    for (KService::Ptr service : KService::allServices())
    {
        QString exec2 = service->exec();
        if (exec2.contains(QLatin1Char(' ')))
            exec2 = exec2.split(QLatin1Char(' ')).first(); // first part of command
        if (exec == exec2)
        {
            servicename = service->name();
            break;
        }
    }

    if (servicename.isEmpty() &&
        exec == QString::fromUtf8("kmailservice")) // kmailto is handled internally by kmailservice
        servicename = i18n("KDE");

    return servicename;
}

QString Helper::readLine()
{
    std::string line;
    if (!std::getline(std::cin, line))
        return {};

    QString qline = QString::fromStdString(line);
    qline.replace(QString::fromUtf8("\\n"), QString::fromUtf8("\n"));
    qline.replace(QString::fromUtf8("\\\\"), QString::fromUtf8("\\"));
    return qline;
}

/* Qt just uses the QWidget* parent as transient parent for native
 * platform dialogs. This makes it impossible to make them transient
 * to a bare QWindow*. So we catch the show event for the QDialog
 * and setTransientParent here instead. */
bool Helper::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Show && obj->inherits("QDialog"))
    {
        QWidget *widget = static_cast<QWidget *>(obj);
        if (wid != 0)
        {
            widget->setAttribute(Qt::WA_NativeWindow, true);
            QWindow *subWindow = widget->windowHandle();
            if (subWindow)
                KWindowSystem::setMainWindow(subWindow, wid);
        }
    }

    return false;
}

void Helper::outputLine(QString line, bool escape)
{
    if (escape)
    {
        line.replace(QString::fromUtf8("\\\\"), QString::fromUtf8("\\"));
        line.replace(QString::fromUtf8("\n"), QString::fromUtf8("\\n"));
    }
    std::cout << line.toStdString() << std::endl;
#ifdef DEBUG_KDE
    std::cerr << "OUTPUT: " << line.toStdString() << std::endl;
#endif
}

bool Helper::readArguments(int mincount)
{
    assert(arguments.isEmpty());
    for (;;)
    {
        QString line = readLine();
        if (!std::cin.good())
        {
            arguments.clear();
            return false;
        }
        if (line == QString::fromUtf8("\\E"))
        {
            arguments_read = true;
            if (arguments.count() >= mincount)
                return true;
            std::cerr << "Not enough arguments for KDE helper." << std::endl;
            return false;
        }
        arguments.append(line);
    }
}

QString Helper::getArgument()
{
    assert(!arguments.isEmpty());
    return arguments.takeFirst();
}

bool Helper::isArgument(const QString &argument)
{
    if (!arguments.isEmpty() && arguments.first() == argument)
    {
        arguments.removeFirst();
        return true;
    }
    return false;
}

bool Helper::allArgumentsUsed()
{
    assert(arguments_read);
    arguments_read = false;
    if (arguments.isEmpty())
        return true;
    std::cerr << "Unused arguments for KDE helper:" << arguments.join(QString::fromUtf8(" ")).toStdString()
              << std::endl;
    arguments.clear();
    return false;
}

long Helper::getArgumentParent()
{
    if (isArgument(QString::fromUtf8("PARENT")))
        return getArgument().toLong();
    return 0;
}
