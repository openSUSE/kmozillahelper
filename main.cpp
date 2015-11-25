/*****************************************************************

Copyright (C) 2009 Lubos Lunak <l.lunak@suse.cz>

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

#include <assert.h>
#include <kaboutdata.h>
#include <kcmdlineargs.h>
#include <kdebug.h>
#include <kdirselectdialog.h>
#include <kfiledialog.h>
#include <kfilefiltercombo.h>
#include <kio/job.h>
#include <kio/netaccess.h>
#include <kmessagebox.h>
#include <kmimetypetrader.h>
#include <knotification.h>
#include <kopenwithdialog.h>
#include <kprotocolinfo.h>
#include <kprotocolmanager.h>
#include <krecentdocument.h>
#include <krun.h>
#include <kservice.h>
#include <kshell.h>
#include <kstandarddirs.h>
#include <kwindowsystem.h>
#include <kprocess.h>
#include <stdio.h>
#include <unistd.h>

//#define DEBUG_KDE

#define HELPER_VERSION 6
#define APP_HELPER_VERSION "0.6.4"

int main( int argc, char* argv[] )
    {
    KAboutData about( "kmozillahelper", "kdelibs4", ki18n( "KMozillaHelper" ), APP_HELPER_VERSION );
    about.setBugAddress( "https://bugzilla.novell.com/enter_bug.cgi");
#if KDE_IS_VERSION( 4, 1, 0 )
    about.setProgramIconName( "firefox" ); // TODO what about other mozilla apps?
#endif
    KCmdLineArgs::init( argc, argv, &about );
    App app;
    app.disableSessionManagement();
    app.setQuitOnLastWindowClosed( false );
    return app.exec();
    }

App::App()
    : input( stdin )
    , output( stdout )
    , notifier( STDIN_FILENO, QSocketNotifier::Read )
    , arguments_read( false )
    {
    connect( &notifier, SIGNAL( activated( int )), SLOT( readCommand()));
    setQuitOnLastWindowClosed( false );
    }

void App::readCommand()
    {
    QString command = readLine();
    if( input.atEnd())
        {
#ifdef DEBUG_KDE
        QTextStream( stderr ) << "EOF, existing." << endl;
#endif
        quit();
        return;
        }
#ifdef DEBUG_KDE
    QTextStream( stderr ) << "COMMAND:" << command << endl;
#endif
    bool status;
    if( command == "CHECK" )
        status = handleCheck();
    else if( command == "GETPROXY" )
        status = handleGetProxy();
    else if( command == "HANDLEREXISTS" )
        status = handleHandlerExists();
    else if( command == "GETFROMEXTENSION" )
        status = handleGetFromExtension();
    else if( command == "GETFROMTYPE" )
        status = handleGetFromType();
    else if( command == "GETAPPDESCFORSCHEME" )
        status = handleGetAppDescForScheme();
    else if( command == "APPSDIALOG" )
        status = handleAppsDialog();
    else if( command == "GETOPENFILENAME" )
        status = handleGetOpenX( false );
    else if( command == "GETOPENURL" )
        status = handleGetOpenX( true );
    else if( command == "GETSAVEFILENAME" )
        status = handleGetSaveX( false );
    else if( command == "GETSAVEURL" )
        status = handleGetSaveX( true );
    else if( command == "GETDIRECTORYFILENAME" )
        status = handleGetDirectoryX( false );
    else if( command == "GETDIRECTORYURL" )
        status = handleGetDirectoryX( true );
    else if( command == "OPEN" )
        status = handleOpen();
    else if( command == "REVEAL" )
        status = handleReveal();
    else if( command == "RUN" )
        status = handleRun();
    else if( command == "GETDEFAULTFEEDREADER" )
        status = handleGetDefaultFeedReader();
    else if( command == "OPENMAIL" )
        status = handleOpenMail();
    else if( command == "OPENNEWS" )
        status = handleOpenNews();
    else if( command == "ISDEFAULTBROWSER" )
        status = handleIsDefaultBrowser();
    else if( command == "SETDEFAULTBROWSER" )
        status = handleSetDefaultBrowser();
    else if( command == "DOWNLOADFINISHED" )
        status = handleDownloadFinished();
    else
        {
        QTextStream( stderr ) << "Unknown command for KDE helper: " << command << endl;
        status = false;
        }
    // status done as \1 (==ok) and \0 (==not ok), because otherwise this cannot happen
    // in normal data (\ is escaped otherwise)
    outputLine( status ? "\\1" : "\\0", false ); // do not escape
    }

bool App::handleCheck()
    {
    if( !readArguments( 1 ))
        return false;
    int version = getArgument().toInt(); // requested version
    if( !allArgumentsUsed())
        return false;
    if( version <= HELPER_VERSION ) // we must have the exact requested version
        return true;
    QTextStream( stderr ) << "KDE helper version too old." << endl;
    return false;
    }

bool App::handleGetProxy()
    {
    if( !readArguments( 1 ))
        return false;
    KUrl url( getArgument());
    if( !allArgumentsUsed())
        return false;
    QString proxy;
    KProtocolManager::slaveProtocol( url, proxy ); 
    if( proxy.isEmpty() || proxy == "DIRECT" ) // TODO return DIRECT if empty?
        {
        outputLine( "DIRECT" );
        return true;
        }
    KUrl proxyurl( proxy );
    if( proxyurl.isValid())
        { // firefox wants this format
        outputLine( "PROXY" " " + proxyurl.host() + ":" + QString::number( proxyurl.port()));
        // TODO there is also "SOCKS " type
        return true;
        }
    return false;
    }

bool App::handleHandlerExists()
    {
    if( !readArguments( 1 ))
        return false;
    QString protocol = getArgument();
    if( !allArgumentsUsed())
        return false;
    return KProtocolInfo::isKnownProtocol( protocol );
    }

bool App::handleGetFromExtension()
    {
    if( !readArguments( 1 ))
        return false;
    QString ext = getArgument();
    if( !allArgumentsUsed())
        return false;
    if( !ext.isEmpty())
        {
        KMimeType::Ptr mime = KMimeType::findByPath( "foo." + ext, 0, true ); // this is findByExtension(), basically
        if( mime )
            return writeMimeInfo( mime );
        }
    return false;
    }

bool App::handleGetFromType()
    {
    if( !readArguments( 1 ))
        return false;
    QString type = getArgument();
    if( !allArgumentsUsed())
        return false;
    KMimeType::Ptr mime = KMimeType::mimeType( type );
    if( mime )
        return writeMimeInfo( mime );
    // firefox also asks for protocol handlers using getfromtype
    QString app = getAppForProtocol( type );
    if( !app.isEmpty())
        {
        outputLine( type );
        outputLine( type ); // TODO probably no way to find a good description
        outputLine( app );
        return true;
        }
    return false;
    }

bool App::writeMimeInfo( KMimeType::Ptr mime )
    {
    KService::Ptr service = KMimeTypeTrader::self()->preferredService( mime->name());
    if( service )
        {
        outputLine( mime->name());
        outputLine( mime->comment());
        outputLine( service->name());
        return true;
        }
    return false;
    }

bool App::handleGetAppDescForScheme()
    {
    if( !readArguments( 1 ))
        return false;
    QString scheme = getArgument();
    if( !allArgumentsUsed())
        return false;
    QString app = getAppForProtocol( scheme );
    if( !app.isEmpty())
        {
        outputLine( app );
        return true;
        }
    return false;
    }

bool App::handleAppsDialog()
    {
    if( !readArguments( 1 ))
        return false;
    QString title = getArgument();
    long wid = getArgumentParent();
    if( !allArgumentsUsed())
        return false;
    KOpenWithDialog dialog( NULL );
    if( !title.isEmpty())
        dialog.setPlainCaption( title );
    dialog.hideNoCloseOnExit();
    dialog.hideRunInTerminal(); // TODO
    if( wid != 0 )
        KWindowSystem::setMainWindow( &dialog, wid );
    if( dialog.exec())
        {
        KService::Ptr service = dialog.service();
        QString command;
        if( service )
            command = service->exec();
        else if( !dialog.text().isEmpty())
            command = dialog.text();
        else
            return false;
        command = command.split( " " ).first(); // only the actual command
        command = KStandardDirs::findExe( command );
        if( command.isEmpty())
            return false;
        outputLine( KUrl( command ).url());
        return true;
        }
    return false;
    }

bool App::handleGetOpenX( bool url )
    {
    if( !readArguments( 4 ))
        return false;
    QString startDir = getArgument();
    QString filter = getArgument().replace("/", "\\/");
    int selectFilter = getArgument().toInt();
    QString title = getArgument();
    bool multiple = isArgument( "MULTIPLE" );
    long wid = getArgumentParent();
    if( !allArgumentsUsed())
        return false;
    KFileDialog dialog( startDir, filter, NULL );
    dialog.setOperationMode( KFileDialog::Opening );
    dialog.setMode(( url ? KFile::Mode( 0 ) : KFile::LocalOnly )
        | KFile::ExistingOnly
        | ( multiple ? KFile::Files : KFile::File ));
    if( title.isEmpty())
        title = i18n( "Open" );
    dialog.setPlainCaption( title );
    QStringList filterslist = dialog.filterWidget()->filters();
    selectFilter = qBound( 0, selectFilter, filterslist.count() - 1 );
    dialog.filterWidget()->setCurrentFilter( filterslist[ selectFilter ] );
    if( wid != 0 )
        KWindowSystem::setMainWindow( &dialog, wid );
    dialog.exec();
    if( url )
        {
        QStringList result = multiple ? dialog.selectedFiles() : ( QStringList() << dialog.selectedFile());
        result.removeAll(QString());
        if( !result.isEmpty())
            {
            outputLine( QString::number( dialog.filterWidget()->currentIndex()));
            foreach( QString str, result )
                outputLine( KUrl( str ).url());
            return true;
            }
        }
    else
        {
        KUrl::List result = multiple ? dialog.selectedUrls() : KUrl::List( dialog.selectedUrl());
        result.removeAll(KUrl());
        if( !result.isEmpty())
            {
            outputLine( QString::number( dialog.filterWidget()->currentIndex()));
            foreach( const KUrl& str, result )
              {
              KUrl newUrl = KIO::NetAccess::mostLocalUrl(str, NULL);
              outputLine( newUrl.toLocalFile() );
              }
            return true;
            }
        }
    return false;
    }

bool App::handleGetSaveX( bool url )
    {
    if( !readArguments( 4 ))
        return false;
    QString startDir = getArgument();
    QString filter = getArgument().replace("/", "\\/");
    int selectFilter = getArgument().toInt();
    QString title = getArgument();
    long wid = getArgumentParent();
    if( !allArgumentsUsed())
        return false;
    KFileDialog dialog( QString(), filter, NULL );
    dialog.setSelection( startDir );
    dialog.setOperationMode( KFileDialog::Saving );
    dialog.setMode( ( url ? KFile::Mode( 0 ) : KFile::LocalOnly ) | KFile::File );
#if KDE_IS_VERSION( 4, 2, 0 )
    dialog.setConfirmOverwrite( true );
#endif
    if( title.isEmpty())
        title = i18n( "Save As" );
    dialog.setPlainCaption( title );
    QStringList filterslist = dialog.filterWidget()->filters();
    selectFilter = qBound( 0, selectFilter, filterslist.count() - 1 );
    dialog.filterWidget()->setCurrentFilter( filterslist[ selectFilter ] );
    if( wid != 0 )
        KWindowSystem::setMainWindow( &dialog, wid );
    dialog.exec();
    if( url )
        {
        KUrl result = dialog.selectedUrl();
        if( result.isValid())
            {
#if !KDE_IS_VERSION( 4, 2, 0 )
            KIO::StatJob* job = KIO::stat( result, KIO::HideProgressInfo );
            if( KIO::NetAccess::synchronousRun( job, NULL )
                && KMessageBox::warningContinueCancelWId( wid,
                    i18n( "A file named \"%1\" already exists. " "Are you sure you want to overwrite it?",
                        result.fileName()), i18n( "Overwrite File?" ), KGuiItem( i18n( "Overwrite" )))
                    == KMessageBox::Cancel )
                {
                return false;
                }
#endif
            outputLine( QString::number( dialog.filterWidget()->currentIndex()));
            outputLine( result.url());
            return true;
            }
        }
    else
        {
        QString result = dialog.selectedFile();
        if( !result.isEmpty())
            {
#if !KDE_IS_VERSION( 4, 2, 0 )
            if( QFile::exists( result )
                && KMessageBox::warningContinueCancelWId( wid,
                    i18n( "A file named \"%1\" already exists. " "Are you sure you want to overwrite it?",
                        result ), i18n( "Overwrite File?" ), KGuiItem( i18n( "Overwrite" )))
                    == KMessageBox::Cancel )
                {
                return false;
                }
#endif
            KRecentDocument::add( result );
            outputLine( QString::number( dialog.filterWidget()->currentIndex()));
            outputLine( result );
            return true;
            }
        }
    return false;
    }

bool App::handleGetDirectoryX( bool url )
    {
    if( !readArguments( 2 ))
        return false;
    QString startDir = getArgument();
    QString title = getArgument();
    long wid = getArgumentParent();
    if( !allArgumentsUsed())
        return false;
    KDirSelectDialog dialog( startDir, !url );
    if( !title.isEmpty())
        dialog.setPlainCaption( title );
    if( wid != 0 )
        KWindowSystem::setMainWindow( &dialog, wid );
    if( dialog.exec() == QDialog::Accepted )
        {
        outputLine( dialog.url().url());
        return true;
        }
    return false;
    }

bool App::handleOpen()
    {
    if( !readArguments( 1 ))
        return false;
    KUrl url = getArgument();
    QString mime;
    if( isArgument( "MIMETYPE" ))
        mime = getArgument();
    if( !allArgumentsUsed())
        return false;
    KApplication::updateUserTimestamp( 0 ); // TODO
    // try to handle the case when the server has broken mimetypes and e.g. claims something is application/octet-stream
    KMimeType::Ptr mimeType = KMimeType::mimeType( mime );
    if( !mime.isEmpty() && mimeType && KMimeTypeTrader::self()->preferredService( mimeType->name()))
        {
        return KRun::runUrl( url, mime, NULL ); // TODO parent
        }
    else
        {
        (void) new KRun( url, NULL ); // TODO parent
    //    QObject::connect( run, SIGNAL( finished()), &app, SLOT( openDone()));
    //    QObject::connect( run, SIGNAL( error()), &app, SLOT( openDone()));
        return true; // TODO check for errors?
        }
    }

bool App::handleReveal()
    {
    if( !readArguments( 1 ))
        return false;
    QString path = getArgument();
    if( !allArgumentsUsed())
        return false;
    KApplication::updateUserTimestamp( 0 ); // TODO
    const KService::List apps = KMimeTypeTrader::self()->query("inode/directory", "Application");
    if (apps.size() != 0)
        {
        QString command = apps.at(0)->exec().split( " " ).first(); // only the actual command
        if (command == "dolphin" || command == "konqueror")
            {
            command = KStandardDirs::findExe( command );
            if( command.isEmpty())
                return false;
            return KProcess::startDetached(command, QStringList() << "--select" << path);
            }
        }
    QFileInfo info(path);
    QString dir = info.dir().path();
    (void) new KRun( KUrl(dir), NULL ); // TODO parent
    return true; // TODO check for errors?
    }

bool App::handleRun()
    {
    if( !readArguments( 2 ))
        return false;
    QString app = getArgument();
    QString arg = getArgument();
    if( !allArgumentsUsed())
        return false;
    KApplication::updateUserTimestamp( 0 ); // TODO
    return KRun::runCommand( KShell::quoteArg( app ) + " " + KShell::quoteArg( arg ), NULL ); // TODO parent, ASN
    }

bool App::handleGetDefaultFeedReader()
    {
    if( !readArguments( 0 ))
        return false;
    // firefox wants the full path
    QString reader = KStandardDirs::findExe( "akregator" ); // TODO there is no KDE setting for this
    if( !reader.isEmpty())
        {
        outputLine( reader );
        return true;
        }
    return false;
    }

bool App::handleOpenMail()
    {
    if( !readArguments( 0 ))
        return false;
    // this is based on ktoolinvocation_x11.cpp, there is no API for this
    KConfig config( "emaildefaults" );
    QString groupname = KConfigGroup( &config, "Defaults" ).readEntry( "Profile", "Default" );
    KConfigGroup group( &config, QString( "PROFILE_%1" ).arg( groupname ));
    QString command = group.readPathEntry( "EmailClient", QString());
    if( command.isEmpty())
        command = "kmail";
    if( group.readEntry( "TerminalClient", false ))
        {
        QString terminal = KConfigGroup( KGlobal::config(), "General" ).readPathEntry( "TerminalApplication", "konsole" );
        command = terminal + " -e " + command;
        }
    KService::Ptr mail = KService::serviceByDesktopName( command.split( " " ).first());
    if( mail )
        {
        KApplication::updateUserTimestamp( 0 ); // TODO
        return KRun::run( *mail, KUrl::List(), NULL ); // TODO parent
        }
    return false;
    }

bool App::handleOpenNews()
    {
    if( !readArguments( 0 ))
        return false;
    KService::Ptr news = KService::serviceByDesktopName( "knode" ); // TODO there is no KDE setting for this
    if( news )
        {
        KApplication::updateUserTimestamp( 0 ); // TODO
        return KRun::run( *news, KUrl::List(), NULL ); // TODO parent
        }
    return false;
    }

bool App::handleIsDefaultBrowser()
    {
    if( !readArguments( 0 ))
        return false;
    QString browser = KConfigGroup( KSharedConfig::openConfig( "kdeglobals" ), "General" )
        .readEntry( "BrowserApplication" );
    return browser == "MozillaFirefox" || browser == "MozillaFirefox.desktop"
        || browser == "!firefox" || browser == "!/usr/bin/firefox"
        || browser == "firefox" || browser == "firefox.desktop";
    }

bool App::handleSetDefaultBrowser()
    {
    if( !readArguments( 1 ))
        return false;
    bool alltypes = ( getArgument() == "ALLTYPES" );
    if( !allArgumentsUsed())
        return false;
    KConfigGroup( KSharedConfig::openConfig( "kdeglobals" ), "General" )
        .writeEntry( "BrowserApplication", "firefox" );
    if( alltypes )
        {
        // TODO there is no API for this and it is a bit complex
        }
    return true;
    }

bool App::handleDownloadFinished()
    {
    if( !readArguments( 1 ))
        return false;
    QString download = getArgument();
    if( !allArgumentsUsed())
        return false;
    // TODO cheat a bit due to i18n freeze - the strings are in the .notifyrc file,
    // taken from KGet, but the notification itself needs the text too.
    // So create it from there.
    KConfig cfg( "kmozillahelper.notifyrc", KConfig::FullConfig, "appdata" );
    QString message = KConfigGroup( &cfg, "Event/downloadfinished" ).readEntry( "Comment" );
    KNotification::event( "downloadfinished", download + " : " + message );
    return true;
    }

#if 0
static bool open_error = false;

void App::openDone()
    {
    // like kde-open - wait 2 second to give error dialogs time to show up
    QTimer::singleShot( 2000, this, SLOT( quit()));
    if( static_cast< KRun* >( sender())->hasError())
        open_error = true;
    }
#endif

QString App::getAppForProtocol( const QString& protocol )
    {
    if( KProtocolInfo::isHelperProtocol( protocol ))
        {
        QString exec = KProtocolInfo::exec( protocol );
        if( !exec.isEmpty())
            {
            if( exec.contains( ' ' ))
                exec = exec.split( ' ' ).first(); // first part of command
            QString servicename;
            if( KService::Ptr service = KService::serviceByDesktopName( exec ))
                servicename = service->name();
            else
                {
                foreach( KService::Ptr service, KService::allServices())
                    {
                    QString exec2 = service->exec();
                    if( exec2.contains( ' ' ))
                        exec2 = exec2.split( ' ' ).first(); // first part of command
                    if( exec == exec2 )
                        {
                        servicename = service->name();
                        break;
                        }
                    }
                if( servicename.isEmpty() && exec == "kmailservice" ) // kmailto is handled internally by kmailservice
                    servicename = i18n( "KDE" );
                }
            return servicename;
            }
        }
    return QString();
    }

QString App::readLine()
    {
    QString line = input.readLine();
    line.replace( "\\n", "\n" );
    line.replace( "\\" "\\", "\\" );
    return line;
    }

void App::outputLine( QString line, bool escape )
    {
    if( escape )
        {
        line.replace( "\\",  "\\" "\\" );
        line.replace( "\n", "\\n" );
        }
    output << line << endl;
#ifdef DEBUG_KDE
    QTextStream( stderr ) << "OUTPUT: " << line << endl;
#endif
    }

bool App::readArguments( int mincount )
    {
    assert( arguments.isEmpty());
    for(;;)
        {
        QString line = readLine();
        if( input.atEnd())
            {
            arguments.clear();
            return false;
            }
        if( line == "\\E" )
            {
            arguments_read = true;
            if( arguments.count() >= mincount )
                return true;
            QTextStream( stderr ) << "Not enough arguments for KDE helper." << endl;
            return false;
            }
        arguments.append( line );
        }
    }

QString App::getArgument()
    {
    assert( !arguments.isEmpty());
    return arguments.takeFirst();
    }

bool App::isArgument( const QString& argument )
    {
    if( !arguments.isEmpty() && arguments.first() == argument )
        {
        arguments.removeFirst();
        return true;
        }
    return false;
    }

bool App::allArgumentsUsed()
    {
    assert( arguments_read );
    arguments_read = false;
    if( arguments.isEmpty())
        return true;
    QTextStream( stderr ) << "Unused arguments for KDE helper:" << arguments.join( " " ) << endl;
    arguments.clear();
    return false;
    }

long App::getArgumentParent()
    {
    if( isArgument( "PARENT" ))
        return getArgument().toLong();
    return 0;
    }

#include "main.h"
