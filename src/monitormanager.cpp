/***************************************************************************
 *   Copyright (C) 2007 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA          *
 ***************************************************************************/


#include "monitormanager.h"
#include "renderer.h"
#include "kdenlivesettings.h"

#include <mlt++/Mlt.h>

#include <QObject>
#include <QTimer>


MonitorManager::MonitorManager(QWidget *parent) :
        QObject(parent),
        m_clipMonitor(NULL),
        m_projectMonitor(NULL),
        m_activeMonitor(NULL),
        m_blocked(false)
{
}

Timecode MonitorManager::timecode()
{
    return m_timecode;
}

void MonitorManager::initMonitors(Monitor *clipMonitor, Monitor *projectMonitor, RecMonitor *recMonitor)
{
    m_clipMonitor = clipMonitor;
    m_projectMonitor = projectMonitor;

    m_monitorsList.append(clipMonitor);
    m_monitorsList.append(projectMonitor);
    m_monitorsList.append(recMonitor);
}

void MonitorManager::appendMonitor(AbstractMonitor *monitor)
{
    if (!m_monitorsList.contains(monitor)) m_monitorsList.append(monitor);
}

void MonitorManager::removeMonitor(AbstractMonitor *monitor)
{
    m_monitorsList.removeAll(monitor);
}

void MonitorManager::activateMonitor(QString name)
{
    kDebug()<<"//ACTIVATING MON: "<<name;
    if (m_blocked || m_clipMonitor == NULL || m_projectMonitor == NULL)
        return;
    if (m_activeMonitor && m_activeMonitor->name() == name)
        return;
    m_activeMonitor = NULL;
    for (int i = 0; i < m_monitorsList.count(); i++) {
        kDebug()<<"PARSING: "<<m_monitorsList.at(i)->name();
        if (m_monitorsList.at(i)->name() == name) {
            m_activeMonitor = m_monitorsList.at(i);
            emit raiseMonitor(m_activeMonitor);
        }
        else m_monitorsList.at(i)->stop();
    }
    if (m_activeMonitor) m_activeMonitor->start();
    emit checkColorScopes();
}

bool MonitorManager::isActive(const QString name) const
{
    return m_activeMonitor ? m_activeMonitor->name() == name: false;
}

void MonitorManager::slotSwitchMonitors(bool activateClip)
{
    if (activateClip)
        activateMonitor("clip");
    else
        activateMonitor("project");
}

void MonitorManager::stopActiveMonitor()
{
    if (m_blocked) return;
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->pause();
    else m_projectMonitor->pause();
}

void MonitorManager::slotPlay()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotPlay();
    else m_projectMonitor->slotPlay();
}

void MonitorManager::slotPause()
{
    stopActiveMonitor();
}

void MonitorManager::slotPlayZone()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotPlayZone();
    else m_projectMonitor->slotPlayZone();
}

void MonitorManager::slotLoopZone()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotLoopZone();
    else m_projectMonitor->slotLoopZone();
}

void MonitorManager::slotRewind(double speed)
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotRewind(speed);
    else m_projectMonitor->slotRewind(speed);
}

void MonitorManager::slotForward(double speed)
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotForward(speed);
    else m_projectMonitor->slotForward(speed);
}

void MonitorManager::slotRewindOneFrame()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotRewindOneFrame();
    else m_projectMonitor->slotRewindOneFrame();
}

void MonitorManager::slotForwardOneFrame()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotForwardOneFrame();
    else m_projectMonitor->slotForwardOneFrame();
}

void MonitorManager::slotRewindOneSecond()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotRewindOneFrame(m_timecode.fps());
    else m_projectMonitor->slotRewindOneFrame(m_timecode.fps());
}

void MonitorManager::slotForwardOneSecond()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotForwardOneFrame(m_timecode.fps());
    else m_projectMonitor->slotForwardOneFrame(m_timecode.fps());
}

void MonitorManager::slotStart()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotStart();
    else m_projectMonitor->slotStart();
}

void MonitorManager::slotEnd()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->slotEnd();
    else m_projectMonitor->slotEnd();
}

void MonitorManager::resetProfiles(Timecode tc)
{
    if (m_blocked) return;
    m_timecode = tc;
    slotResetProfiles();
    //QTimer::singleShot(300, this, SLOT(slotResetProfiles()));
}

void MonitorManager::slotResetProfiles()
{
    if (m_blocked) return;
    if (m_projectMonitor == NULL || m_clipMonitor == NULL) return;
    QString active = m_activeMonitor ? m_activeMonitor->name() : QString();
    activateMonitor("clip");
    m_clipMonitor->resetProfile(KdenliveSettings::current_profile());
    m_clipMonitor->updateTimecodeFormat();
    activateMonitor("project");
    m_projectMonitor->resetProfile(KdenliveSettings::current_profile());
    m_projectMonitor->updateTimecodeFormat();
    //m_projectMonitor->refreshMonitor(true);
    if (!active.isEmpty()) activateMonitor(active);
}

void MonitorManager::slotRefreshCurrentMonitor()
{
    if (m_activeMonitor == m_clipMonitor) m_clipMonitor->refreshMonitor();
    else m_projectMonitor->refreshMonitor();
}

void MonitorManager::slotUpdateAudioMonitoring()
{
    // if(...) added since they are 0x0 when the config wizard is running! --Granjow
    if (m_clipMonitor) {
        m_clipMonitor->render->analyseAudio = KdenliveSettings::monitor_audio();
    }
    if (m_projectMonitor) {
        m_projectMonitor->render->analyseAudio = KdenliveSettings::monitor_audio();
    }
}

void MonitorManager::updateScopeSource()
{
    emit checkColorScopes();
}

AbstractRender *MonitorManager::activeRenderer()
{
    if (m_activeMonitor) return m_activeMonitor->abstractRender();
    return NULL;
}

#include "monitormanager.moc"
