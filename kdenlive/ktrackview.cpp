/***************************************************************************
                         kmmtimelinetrackview.cpp  -  description
                            -------------------
   begin                : Wed Aug 7 2002
   copyright            : (C) 2002 by Jason Wood
   email                : jasonwood@blueyonder.co.uk
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <iostream>
#include <math.h>

#include <qpainter.h>
#include <qcursor.h>
#include <qpopupmenu.h>

#include <kdebug.h>

#include "ktrackview.h"
#include "ktimeline.h"
#include "kmmtrackpanel.h"
#include "kselectclipcommand.h"
#include "trackpanelfunction.h"

KTrackView::KTrackView( KTimeLine &timeLine, QWidget *parent, const char *name ) :
		QWidget( parent, name ),
		m_timeline( timeLine ),
		m_trackBaseNum( -1 ),
		m_panelUnderMouse( 0 ),
		m_function( 0 ),
		m_dragFunction( 0 )
{
	// we draw everything ourselves, no need to draw background.
	setBackgroundMode( Qt::NoBackground );
	setMouseTracking( true );
	m_bufferInvalid = false;

	setAcceptDrops( true );
}

KTrackView::~KTrackView()
{
}

void KTrackView::resizeEvent( QResizeEvent *event )
{
	m_backBuffer.resize( event->size().width(), event->size().height() );
	drawBackBuffer();
}

void KTrackView::paintEvent( QPaintEvent *event )
{
	if ( m_bufferInvalid ) {
		drawBackBuffer();
		m_bufferInvalid = false;
	}

	QPainter painter( this );

	painter.drawPixmap( event->rect().x(), event->rect().y(),
	                    m_backBuffer,
	                    event->rect().x(), event->rect().y(),
	                    event->rect().width(), event->rect().height() );
}

void KTrackView::drawBackBuffer()
{
	QPainter painter( &m_backBuffer );

	painter.fillRect( 0, 0, width(), height(), palette().active().background() );

	KTrackPanel *panel = m_timeline.trackList().first();
	while ( panel != 0 ) {
		int y = panel->y() - this->y();

		QRect rect( 0, y, width(), panel->height() );
		panel->drawToBackBuffer( painter, rect );

		panel = m_timeline.trackList().next();
	}
}

KTrackPanel *KTrackView::panelAt( int y )
{
	KTrackPanel * panel = m_timeline.trackList().first();

	while ( panel != 0 ) {
		int sy = panel->y() - this->y();
		int ey = sy + panel->height();

		if ( ( y >= sy ) && ( y < ey ) ) break;

		panel = m_timeline.trackList().next();
	}

	return panel;
}

/** Invalidate the back buffer, alerting the trackview that it should redraw itself. */
void KTrackView::invalidateBackBuffer()
{
	m_bufferInvalid = true;
	update();
}

void KTrackView::registerFunction(const QString &name, TrackPanelFunction *function)
{
	m_factory.registerFunction(name, function);
}

/** This event occurs when a mouse button is pressed. */
void KTrackView::mousePressEvent( QMouseEvent *event )
{
	if ( event->button() == RightButton ) {
		emit rightButtonPressed();
	} else {
		KTrackPanel *panel = panelAt( event->y() );
		if ( m_panelUnderMouse != 0 ) {
			kdWarning() << "Error - mouse Press Event with panel already under mouse" << endl;
		}
		if ( panel ) {
			if ( event->button() == LeftButton ) {
				bool result = false;
				m_function = getApplicableFunction( panel, m_timeline.editMode(), event );
				if ( m_function ) result = m_function->mousePressed( panel, event );
				if ( result ) {
					m_panelUnderMouse = panel;
				} else {
					m_function = 0;
				}
			}
		}
	}
}

/** This event occurs when a mouse button is released. */
void KTrackView::mouseReleaseEvent( QMouseEvent *event )
{
	if ( m_panelUnderMouse ) {
		if ( event->button() == LeftButton ) {
			bool result = false;
			if ( m_function ) {
				result = m_function->mouseReleased( m_panelUnderMouse, event );
				m_function = 0;
			}

			if(result) m_panelUnderMouse = 0;
		}
	}
}

/** This event occurs when the mouse has been moved. */
void KTrackView::mouseMoveEvent( QMouseEvent *event )
{
	if ( m_panelUnderMouse ) {
		if ( event->state() & LeftButton ) {
			bool result = false;
			if ( m_function ) result = m_function->mouseMoved( m_panelUnderMouse, event );
			if ( !result ) {
				m_panelUnderMouse = 0;
				m_function = 0;
			}
		} else {
			if ( m_function ) {
				m_function->mouseReleased( m_panelUnderMouse, event );
				m_function = 0;
			}
			m_panelUnderMouse = 0;
		}
	} else {
		KTrackPanel *panel = panelAt( event->y() );
		if ( panel ) {
			QCursor result( Qt::ArrowCursor );

			TrackPanelFunction *function = getApplicableFunction( panel, m_timeline.editMode(), event );
			if ( function ) result = function->getMouseCursor( panel, event );

			setCursor( result );
		} else {
			setCursor( QCursor( Qt::ArrowCursor ) );
		}
	}
}

// virtual
void KTrackView::dragEnterEvent ( QDragEnterEvent *event )
{
	kdWarning() << "Drag enter event" << endl;

	// If there is a "panelUnderMouse" if means that the drag was initiated by one of the panels. Otherwise, the drag has reached
	// the timeline from somewhere else.
	if(m_panelUnderMouse) {
		if((!m_dragFunction) || (!m_dragFunction->dragEntered(m_panelUnderMouse, event))) {
			m_panelUnderMouse = 0;
		}
	} else {
		KTrackPanel *panel = m_timeline.trackList().first();
		if(panel) {
			if((m_dragFunction) && (m_dragFunction->dragEntered(panel, event))) {
				m_panelUnderMouse = panel;
			}
		}
	}
}

// virtual
void KTrackView::dragMoveEvent ( QDragMoveEvent *event )
{
	if(m_panelUnderMouse) {
		if((!m_dragFunction) || (!m_dragFunction->dragMoved(m_panelUnderMouse, event))) {
			m_panelUnderMouse = 0;
		}
	}
}

// virtual
void KTrackView::dragLeaveEvent ( QDragLeaveEvent *event )
{
	if(m_panelUnderMouse) {
		if(m_dragFunction) m_dragFunction->dragLeft(m_panelUnderMouse, event);
		m_panelUnderMouse = 0;
	}
}

// virtual
void KTrackView::dropEvent ( QDropEvent *event )
{
	if(m_panelUnderMouse) {
		if((!m_dragFunction) || (!m_dragFunction->dragDropped(m_panelUnderMouse, event))) {
			m_panelUnderMouse = 0;
		}
	}
}

TrackPanelFunction *KTrackView::getApplicableFunction(KTrackPanel *panel, const QString &editMode, QMouseEvent *event)
{
	TrackPanelFunction *function = 0;

	QStringList list = panel->applicableFunctions(editMode);
	QStringList::iterator itt = list.begin();

	while(itt != list.end()) {
		TrackPanelFunction *testFunction = m_factory.function(*itt);
		if(testFunction) {
			if(testFunction->mouseApplies(panel, event)) {
				function = testFunction;
				break;
			}
		}

		++itt;
	}

	return function;
}

void KTrackView::setDragFunction(const QString &name)
{
	m_dragFunction = m_factory.function(name);
}


/**
bool KTrackPanel::mouseMoved( QMouseEvent *event )
{
	bool result = false;

	if ( m_function ) result = m_function->mouseMoved( event );

	if ( !result ) m_function = 0;

	return result;
}

QCursor KTrackPanel::getMouseCursor( QMouseEvent *event )
{


	return result;
}

// virtual
bool KTrackPanel::dragLeft ( QDragLeaveEvent *event )
{
	bool result = false;

	if(m_dragFunction) result = m_dragFunction->dragLeft(event);

	return result;
}

// virtual
bool KTrackPanel::dragDropped ( QDropEvent *event )
{
	bool result = false;

	if(m_dragFunction) result = m_dragFunction->dragDropped(event);

	return result;
}
*/
