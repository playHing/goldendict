/* This file is (c) 2008-2009 Konstantin Isakov <ikm@users.berlios.de>
 * Part of GoldenDict. Licensed under GPLv3 or later, see the LICENSE file */

#include "articleview.hh"
#include "externalviewer.hh"
#include <QMessageBox>
#include <QWebHitTestResult>
#include <QMenu>
#include <QDesktopServices>

#ifdef Q_OS_WIN32
#include <windows.h>
#include <mmsystem.h> // For PlaySound
#endif

ArticleView::ArticleView( QWidget * parent, ArticleNetworkAccessManager & nm,
                          Instances::Groups const & groups_, bool popupView_ ):
  QFrame( parent ),
  articleNetMgr( nm ),
  groups( groups_ ),
  popupView( popupView_ )
{
  ui.setupUi( this );

  ui.definition->setContextMenuPolicy( Qt::CustomContextMenu );

  ui.definition->page()->setLinkDelegationPolicy( QWebPage::DelegateAllLinks );

  ui.definition->page()->setNetworkAccessManager( &articleNetMgr );

  connect( ui.definition, SIGNAL( loadFinished( bool ) ),
           this, SLOT( loadFinished( bool ) ) );
  
  connect( ui.definition, SIGNAL( titleChanged( QString const & ) ),
           this, SLOT( handleTitleChanged( QString const & ) ) );

  connect( ui.definition, SIGNAL( urlChanged( QUrl const & ) ),
           this, SLOT( handleUrlChanged( QUrl const & ) ) );

  connect( ui.definition, SIGNAL( customContextMenuRequested( QPoint const & ) ),
           this, SLOT( contextMenuRequested( QPoint const & ) ) );

  connect( ui.definition, SIGNAL( linkClicked( QUrl const & ) ),
           this, SLOT( linkClicked( QUrl const & ) ) );
}

ArticleView::~ArticleView()
{
  cleanupTemp();
  
  #ifdef Q_OS_WIN32
  if ( winWavData.size() )
  {
    // If we were playing some sound some time ago, make sure it stopped
    // playing before freeing the waveform memory.
    PlaySoundA( 0, 0, 0 );
  }
  #endif
}

void ArticleView::showDefinition( QString const & word, QString const & group )
{
  QUrl req;

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );
  req.addQueryItem( "word", word );
  req.addQueryItem( "group", group );

  ui.definition->load( req );
}

void ArticleView::showNotFound( QString const & word, QString const & group )
{
  QUrl req;

  req.setScheme( "gdlookup" );
  req.setHost( "localhost" );
  req.addQueryItem( "word", word );
  req.addQueryItem( "group", group );
  req.addQueryItem( "notfound", "1" );

  ui.definition->load( req );
}

void ArticleView::showAnticipation()
{
  ui.definition->setHtml( "" );
  //ui.definition->setCursor( Qt::WaitCursor );
  QApplication::setOverrideCursor( Qt::WaitCursor );
}

void ArticleView::loadFinished( bool )
{
  //ui.definition->unsetCursor();
  QApplication::restoreOverrideCursor();
}

void ArticleView::handleTitleChanged( QString const & title )
{
  emit titleChanged( this, title );
}

void ArticleView::handleUrlChanged( QUrl const & url )
{
  QIcon icon;

  QString group = getGroup( url );

  if ( group.size() )
  {
    // Find the group's instance corresponding to the fragment value
    for( unsigned x = 0; x < groups.size(); ++x )
      if ( groups[ x ].name == group )
      {
        // Found it

        if ( groups[ x ].icon.size() )
          icon = QIcon( ":/flags/" + groups[ x ].icon );
        else
          break;
      }
  }

  emit iconChanged( this, icon );
}

QString ArticleView::getGroup( QUrl const & url )
{
  if ( url.scheme() == "gdlookup" && url.hasQueryItem( "group" ) )
    return url.queryItemValue( "group" );

  return QString();
}

void ArticleView::cleanupTemp()
{
  if ( desktopOpenedTempFile.size() )
  {
    QFile( desktopOpenedTempFile ).remove();
    desktopOpenedTempFile.clear();
  }  
}


void ArticleView::linkClicked( QUrl const & url )
{
  openLink( url, ui.definition->url() );
}

void ArticleView::openLink( QUrl const & url, QUrl const & ref )
{
  printf( "clicked %s\n", url.toString().toLocal8Bit().data() );

  if ( url.scheme() == "bword" )
    showDefinition( url.host().startsWith( "xn--" ) ?
                      QUrl::fromPunycode( url.host().toLatin1() ) :
                      url.host(),
                    getGroup( ref ) );
  else
  if ( url.scheme() == "gdlookup" ) // Plain html links inherit gdlookup scheme
    showDefinition( url.path().mid( 1 ),
                    getGroup( ref ) );
  else
  if ( url.scheme() == "bres" || url.scheme() == "gdau" )
  {
    // Download it

    vector< char > data;
    bool found = false;


    if ( url.scheme() == "gdau" && url.host() == "search" && groups.size() )
    {
      // Since searches should be limited to current group, we just do them
      // here ourselves since otherwise we'd need to pass group id to netmgr
      // and it should've been having knowledge of the current groups, too.

      QString currentGroup = getGroup( ref );

      for( unsigned x = 0; x < groups.size(); ++x )
        if ( groups[ x ].name == currentGroup )
        {
          for( unsigned y = 0; y < groups[ x ].dictionaries.size(); ++y )
          {
            try
            {
              groups[ x ].dictionaries[ y ]->getResource(
                url.path().mid( 1 ).toUtf8().data(), data );

              found = true;
              break;
            }
            catch( Dictionary::exNoSuchResource & )
            {
              continue;
            }
          }
          break;
        }
    }

    QString contentType;

    if ( !found && !articleNetMgr.getResource( url, data, contentType ) )
    {
      QMessageBox::critical( this, tr( "GoldenDict" ), tr( "The referenced resource doesn't exist." ) );
      return;
    }

    if ( url.scheme() == "gdau" )
    {
#ifdef Q_OS_WIN32
      // Windows-only: use system PlaySound function

      if ( winWavData.size() )
        PlaySoundA( 0, 0, 0 ); // Stop any currently playing sound to make sure
                               // previous data isn't used anymore
                               // 
      winWavData = data;

      PlaySoundA( &winWavData.front(), 0,
                  SND_ASYNC | SND_MEMORY | SND_NODEFAULT | SND_NOWAIT );
#else

      // Use external viewer to play the file
      try
      {
        ExternalViewer * viewer = new ExternalViewer( this, data, ".wav", "play" );

        try
        {
          viewer->start();

          // Once started, it will erase itself
        }
        catch( ... )
        {
          delete viewer;
          throw;
        }
      }
      catch( ExternalViewer::Ex & e )
      {
        QMessageBox::critical( this, tr( "GoldenDict" ), tr( "Failed to run a player to play sound file: %1" ).arg( e.what() ) );
      }
      
#endif

      return;
    }
    
    // Create a temporary file

    
    // Remove the one previously used, if any
    cleanupTemp();

    {
      QTemporaryFile tmp( QDir::temp().filePath( "XXXXXX-" + url.path().section( '/', -1 ) ), this );
  
      if ( !tmp.open() || tmp.write( &data.front(), data.size() ) != data.size() )
      {
        QMessageBox::critical( this, tr( "GoldenDict" ), tr( "Failed to create temporary file." ) );
        return;
      }

      tmp.setAutoRemove( false );
  
      desktopOpenedTempFile = tmp.fileName();
    }

    if ( !QDesktopServices::openUrl( QUrl::fromLocalFile( desktopOpenedTempFile ) ) )
      QMessageBox::critical( this, tr( "GoldenDict" ), tr( "Failed to auto-open resource file, try opening manually: %1." ).arg( desktopOpenedTempFile ) );
  }
  else
  if ( url.scheme() == "http" || url.scheme() == "https" ||
      url.scheme() == "ftp" || url.scheme() == "mailto" )
  {
    // Use the system handler for the conventional internet links
    QDesktopServices::openUrl( url );
  }
}

void ArticleView::contextMenuRequested( QPoint const & pos )
{
  // Is that a link? Is there a selection?

  QWebHitTestResult r = ui.definition->page()->currentFrame()->
                          hitTestContent( pos );

  QMenu menu( this );


  QAction * followLink = 0;
  QAction * followLinkNewTab = 0;
  QAction * lookupSelection = 0;
  QAction * lookupSelectionNewTab = 0;

  if ( !r.linkUrl().isEmpty() )
  {
    followLink = new QAction( tr( "&Open Link" ), &menu );
    menu.addAction( followLink );
    
    if ( !popupView )
    {
      followLinkNewTab = new QAction( tr( "Open Link in New &Tab" ), &menu );
      menu.addAction( followLinkNewTab );
    }
  }

  QString selectedText = ui.definition->selectedText();
  
  if ( selectedText.size() )
  {
    lookupSelection = new QAction( tr( "&Look up \"%1\"" ).arg( ui.definition->selectedText() ), &menu );
    menu.addAction( lookupSelection );
    
    if ( !popupView )
    {
      lookupSelectionNewTab = new QAction( tr( "Look up \"%1\" in &New Tab" ).arg( ui.definition->selectedText() ), &menu );
      menu.addAction( lookupSelectionNewTab );
    }
  }

  if ( !menu.isEmpty() )
  {
    QAction * result = menu.exec( ui.definition->mapToGlobal( pos ) );

    if ( result == followLink )
      linkClicked( r.linkUrl() );
    else
    if ( result == lookupSelection )
      showDefinition( selectedText, getGroup( ui.definition->url() ) );
    else
    if ( !popupView )
    {
      if (result == followLinkNewTab )
        emit openLinkInNewTab( r.linkUrl(), ui.definition->url() );
      else
      if ( result == lookupSelectionNewTab )
        emit showDefinitionInNewTab( selectedText, getGroup( ui.definition->url() ) );
    }
  }
#if 0
  printf( "%s\n", r.linkUrl().isEmpty() ? "null" : "not null" );

  printf( "url = %s\n", r.linkUrl().toString().toLocal8Bit().data() );
  printf( "title = %s\n", r.title().toLocal8Bit().data() );
#endif
}

void ArticleView::showEvent( QShowEvent * ev )
{
  QFrame::showEvent( ev );

  ui.searchFrame->hide();
}
