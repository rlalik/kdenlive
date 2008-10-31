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

#include <KDebug>
#include <KStandardDirs>
#include <KMessageBox>
#include <KLocale>
#include <KFileDialog>
#include <KIO/NetAccess>
#include <KApplication>

#include <mlt++/Mlt.h>

#include "kdenlivedoc.h"
#include "docclipbase.h"
#include "profilesdialog.h"
#include "kdenlivesettings.h"
#include "renderer.h"
#include "clipmanager.h"
#include "addfoldercommand.h"
#include "editfoldercommand.h"
#include "titlewidget.h"
#include "mainwindow.h"


KdenliveDoc::KdenliveDoc(const KUrl &url, const KUrl &projectFolder, QUndoGroup *undoGroup, const QString &profileName, const QPoint tracks, MainWindow *parent): QObject(parent), m_render(NULL), m_url(url), m_projectFolder(projectFolder), m_commandStack(new QUndoStack(undoGroup)), m_modified(false), m_documentLoadingProgress(0), m_documentLoadingStep(0.0), m_startPos(0), m_zoom(7), m_autosave(NULL) {
    m_clipManager = new ClipManager(this);
    if (!url.isEmpty()) {
        QString tmpFile;
        if (KIO::NetAccess::download(url.path(), tmpFile, parent)) {
            QFile file(tmpFile);
            m_document.setContent(&file, false);
            file.close();
            QDomNode infoXmlNode = m_document.elementsByTagName("kdenlivedoc").at(0);
            QDomNode westley = m_document.elementsByTagName("westley").at(0);
            if (!infoXmlNode.isNull()) {
                QDomElement infoXml = infoXmlNode.toElement();
                QString profilePath = infoXml.attribute("profile");
                m_startPos = infoXml.attribute("position").toInt();
                m_zoom = infoXml.attribute("zoom", "7").toInt();
                setProfilePath(profilePath);
                double version = infoXml.attribute("version").toDouble();
                if (version < 0.8) convertDocument(version);
                else {
                    //delete all mlt producers and instead, use Kdenlive saved producers
                    /*QDomNodeList prods = m_document.elementsByTagName("producer");
                    int maxprod = prods.count();
                    int pos = 0;
                    for (int i = 0; i < maxprod; i++) {
                        QDomNode m = prods.at(pos);
                        QString prodId = m.toElement().attribute("id");
                        if (prodId == "black" || prodId.startsWith("slowmotion"))
                            pos++;
                        else westley.removeChild(m);
                    }*/
                    /*prods = m_document.elementsByTagName("kdenlive_producer");
                    maxprod = prods.count();
                    for (int i = 0; i < maxprod; i++) {
                        prods.at(0).toElement().setTagName("producer");
                        westley.insertBefore(prods.at(0), QDomNode());
                    }*/
                }
                QDomElement e;
                QDomNodeList producers = m_document.elementsByTagName("producer");
                QDomNodeList infoproducers = m_document.elementsByTagName("kdenlive_producer");
                const int max = producers.count();
                const int infomax = infoproducers.count();

                if (max > 0) {
                    m_documentLoadingStep = 100.0 / (max + infomax + m_document.elementsByTagName("entry").count());
                    parent->slotGotProgressInfo(i18n("Loading project clips"), (int) m_documentLoadingProgress);
                }

                for (int i = 0; i < max; i++) {
                    e = producers.item(i).cloneNode().toElement();
                    if (m_documentLoadingStep > 0) {
                        m_documentLoadingProgress += m_documentLoadingStep;
                        parent->slotGotProgressInfo(QString(), (int) m_documentLoadingProgress);
                        //qApp->processEvents();
                    }
                    QString prodId = e.attribute("id");
                    if (!e.isNull() && prodId != "black" && !prodId.startsWith("slowmotion")/*&& prodId.toInt() > 0*/) {
                        // addClip(e, prodId, false);
                        kDebug() << "// PROD: " << prodId;
                    }
                }

                for (int i = 0; i < infomax; i++) {
                    e = infoproducers.item(i).cloneNode().toElement();
                    if (m_documentLoadingStep > 0) {
                        m_documentLoadingProgress += m_documentLoadingStep;
                        parent->slotGotProgressInfo(QString(), (int) m_documentLoadingProgress);
                        //qApp->processEvents();
                    }
                    QString prodId = e.attribute("id");
                    if (!e.isNull() && prodId != "black" && !prodId.startsWith("slowmotion")) {
                        e.setTagName("producer");
                        addClipInfo(e, prodId);
                        kDebug() << "// NLIVE PROD: " << prodId;
                    }
                }

                QDomNode markers = m_document.elementsByTagName("markers").at(0);
                if (!markers.isNull()) {
                    QDomNodeList markerslist = markers.childNodes();
                    int maxchild = markerslist.count();
                    for (int k = 0; k < maxchild; k++) {
                        e = markerslist.at(k).toElement();
                        if (e.tagName() == "marker") {
                            m_clipManager->getClipById(e.attribute("id"))->addSnapMarker(GenTime(e.attribute("time").toDouble()), e.attribute("comment"));
                        }
                    }
                    westley.removeChild(markers);
                }
                m_document.removeChild(infoXmlNode);

                kDebug() << "Reading file: " << url.path() << ", found clips: " << producers.count();
            } else {
                parent->slotGotProgressInfo(i18n("File %1 is not a Kdenlive project file."), 100);
                kWarning() << "  NO KDENLIVE INFO FOUND IN FILE: " << url.path();
                m_document = createEmptyDocument(tracks.x(), tracks.y());
                setProfilePath(profileName);
            }
            KIO::NetAccess::removeTempFile(tmpFile);
        } else {
            KMessageBox::error(parent, KIO::NetAccess::lastErrorString());
            parent->slotGotProgressInfo(i18n("File %1 is not a Kdenlive project file."), 100);
            m_document = createEmptyDocument(tracks.x(), tracks.y());
            setProfilePath(profileName);
        }
    } else {
        m_document = createEmptyDocument(tracks.x(), tracks.y());
        setProfilePath(profileName);
    }
    m_scenelist = m_document.toString();
    kDebug() << "KDEnnlive document, init timecode: " << m_fps;
    if (m_fps == 30000.0 / 1001.0) m_timecode.setFormat(30, true);
    else m_timecode.setFormat((int) m_fps);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    connect(m_autoSaveTimer, SIGNAL(timeout()), this, SLOT(slotAutoSave()));
}

KdenliveDoc::~KdenliveDoc() {
    delete m_commandStack;
    delete m_clipManager;
    delete m_autoSaveTimer;
    m_autosave->remove();
}

QDomDocument KdenliveDoc::createEmptyDocument(const int videotracks, const int audiotracks) {
    // Creating new document
    QDomDocument doc;
    QDomElement westley = doc.createElement("westley");
    doc.appendChild(westley);

    QDomElement tractor = doc.createElement("tractor");
    tractor.setAttribute("id", "maintractor");
    QDomElement multitrack = doc.createElement("multitrack");
    QDomElement playlist = doc.createElement("playlist");
    playlist.setAttribute("id", "black_track");
    westley.appendChild(playlist);


    // create playlists
    int total = audiotracks + videotracks + 1;

    for (int i = 1; i < total; i++) {
        QDomElement playlist = doc.createElement("playlist");
        playlist.setAttribute("id", "playlist" + QString::number(i));
        westley.appendChild(playlist);
    }

    QDomElement track0 = doc.createElement("track");
    track0.setAttribute("producer", "black_track");
    tractor.appendChild(track0);

    // create audio tracks
    for (int i = 1; i < audiotracks + 1; i++) {
        QDomElement track = doc.createElement("track");
        track.setAttribute("producer", "playlist" + QString::number(i));
        track.setAttribute("hide", "video");
        tractor.appendChild(track);
    }

    // create video tracks
    for (int i = audiotracks + 1; i < total; i++) {
        QDomElement track = doc.createElement("track");
        track.setAttribute("producer", "playlist" + QString::number(i));
        tractor.appendChild(track);
    }

    for (uint i = 2; i < total ; i++) {
        QDomElement transition = doc.createElement("transition");
        transition.setAttribute("always_active", "1");

        QDomElement property = doc.createElement("property");
        property.setAttribute("name", "a_track");
        QDomText value = doc.createTextNode(QString::number(1));
        property.appendChild(value);
        transition.appendChild(property);

        property = doc.createElement("property");
        property.setAttribute("name", "b_track");
        value = doc.createTextNode(QString::number(i));
        property.appendChild(value);
        transition.appendChild(property);

        property = doc.createElement("property");
        property.setAttribute("name", "mlt_service");
        value = doc.createTextNode("mix");
        property.appendChild(value);
        transition.appendChild(property);

        property = doc.createElement("property");
        property.setAttribute("name", "combine");
        value = doc.createTextNode("1");
        property.appendChild(value);
        transition.appendChild(property);

        property = doc.createElement("property");
        property.setAttribute("name", "internal_added");
        value = doc.createTextNode("237");
        property.appendChild(value);
        transition.appendChild(property);
        tractor.appendChild(transition);
    }
    westley.appendChild(tractor);
    return doc;
}


void KdenliveDoc::syncGuides(QList <Guide *> guides) {
    QDomDocument doc;
    QDomElement e;
    m_guidesXml.clear();
    m_guidesXml = doc.createElement("guides");

    for (int i = 0; i < guides.count(); i++) {
        e = doc.createElement("guide");
        e.setAttribute("time", guides.at(i)->position().ms() / 1000);
        e.setAttribute("comment", guides.at(i)->label());
        m_guidesXml.appendChild(e);
    }
    emit guidesUpdated();
}

QDomElement KdenliveDoc::guidesXml() const {
    return m_guidesXml;
}

void KdenliveDoc::slotAutoSave() {
    if (m_render) {
        if (!m_autosave->isOpen() && !m_autosave->open(QIODevice::ReadWrite)) {
            // show error: could not open the autosave file
            kDebug() << "ERROR; CANNOT CREATE AUTOSAVE FILE";
        }
        kDebug() << "// AUTOSAVE FILE: " << m_autosave->fileName();
        QDomDocument doc;
        doc.setContent(m_render->sceneList());
        saveSceneList(m_autosave->fileName(), doc);
    }
}

void KdenliveDoc::setZoom(int factor) {
    m_zoom = factor;
}

int KdenliveDoc::zoom() const {
    return m_zoom;
}

void KdenliveDoc::convertDocument(double version) {
    // Opening a old Kdenlive document
    if (version == 0.7) {
        // TODO: convert 0.7 files to the new document format.
        return;
    }
    QDomNode westley = m_document.elementsByTagName("westley").at(1);
    QDomNode tractor = m_document.elementsByTagName("tractor").at(0);
    QDomNode kdenlivedoc = m_document.elementsByTagName("kdenlivedoc").at(0);
    QDomNode multitrack = m_document.elementsByTagName("multitrack").at(0);
    QDomNodeList playlists = m_document.elementsByTagName("playlist");

    m_startPos = kdenlivedoc.toElement().attribute("timeline_position").toInt();

    QDomNode props = m_document.elementsByTagName("properties").at(0).toElement();
    QString profile = props.toElement().attribute("videoprofile");
    if (profile == "dv_wide") profile = "dv_pal_wide";
    setProfilePath(profile);

    // move playlists outside of tractor and add the tracks instead
    int max = playlists.count();
    for (int i = 0; i < max; i++) {
        QDomNode n = playlists.at(i);
        westley.insertBefore(n, QDomNode());
        QDomElement pl = n.toElement();
        QDomElement track = m_document.createElement("track");
        QString trackType = pl.attribute("hide");
        if (!trackType.isEmpty()) track.setAttribute("hide", trackType);
        QString playlist_id =  pl.attribute("id");
        if (playlist_id.isEmpty()) {
            playlist_id = "black_track";
            pl.setAttribute("id", playlist_id);
        }
        track.setAttribute("producer", playlist_id);
        //tractor.appendChild(track);
        tractor.insertAfter(track, QDomNode());
    }
    tractor.removeChild(multitrack);

    // audio track mixing transitions should not be added to track view, so add required attribute
    QDomNodeList transitions = m_document.elementsByTagName("transition");
    max = transitions.count();
    for (int i = 0; i < max; i++) {
        QDomElement tr = transitions.at(i).toElement();
        if (tr.attribute("combine") == "1" && tr.attribute("mlt_service") == "mix") {
            QDomElement property = m_document.createElement("property");
            property.setAttribute("name", "internal_added");
            QDomText value = m_document.createTextNode("237");
            property.appendChild(value);
            tr.appendChild(property);
        } else {
            // convert transition
            QDomNamedNodeMap attrs = tr.attributes();
            for (unsigned int j = 0; j < attrs.count(); j++) {
                QString attrName = attrs.item(j).nodeName();
                if (attrName != "in" && attrName != "out" && attrName != "id") {
                    QDomElement property = m_document.createElement("property");
                    property.setAttribute("name", attrName);
                    QDomText value = m_document.createTextNode(attrs.item(j).nodeValue());
                    property.appendChild(value);
                    tr.appendChild(property);
                }
            }
        }
    }

    // move transitions after tracks
    for (int i = 0; i < max; i++) {
        tractor.insertAfter(transitions.at(0), QDomNode());
    }

    // Fix filters format
    QDomNodeList entries = m_document.elementsByTagName("entry");
    max = entries.count();
    for (int i = 0; i < max; i++) {
        QString last_id;
        int effectix = 0;
        QDomNode m = entries.at(i).firstChild();
        while (!m.isNull()) {
            if (m.toElement().tagName() == "filter") {
                QDomElement filt = m.toElement();
                QDomNamedNodeMap attrs = filt.attributes();
                QString current_id = filt.attribute("kdenlive_id");
                if (current_id != last_id) {
                    effectix++;
                    last_id = current_id;
                }
                QDomElement e = m_document.createElement("property");
                e.setAttribute("name", "kdenlive_ix");
                QDomText value = m_document.createTextNode(QString::number(effectix));
                e.appendChild(value);
                filt.appendChild(e);
                for (int j = 0; j < attrs.count(); j++) {
                    QDomAttr a = attrs.item(j).toAttr();
                    if (!a.isNull()) {
                        kDebug() << " FILTER; adding :" << a.name() << ":" << a.value();
                        QDomElement e = m_document.createElement("property");
                        e.setAttribute("name", a.name());
                        QDomText value = m_document.createTextNode(a.value());
                        e.appendChild(value);
                        filt.appendChild(e);

                    }
                }
            }
            m = m.nextSibling();
        }
    }

    /*
        QDomNodeList filters = m_document.elementsByTagName("filter");
        max = filters.count();
        QString last_id;
        int effectix = 0;
        for (int i = 0; i < max; i++) {
            QDomElement filt = filters.at(i).toElement();
            QDomNamedNodeMap attrs = filt.attributes();
     QString current_id = filt.attribute("kdenlive_id");
     if (current_id != last_id) {
         effectix++;
         last_id = current_id;
     }
     QDomElement e = m_document.createElement("property");
            e.setAttribute("name", "kdenlive_ix");
            QDomText value = m_document.createTextNode(QString::number(1));
            e.appendChild(value);
            filt.appendChild(e);
            for (int j = 0; j < attrs.count(); j++) {
                QDomAttr a = attrs.item(j).toAttr();
                if (!a.isNull()) {
                    kDebug() << " FILTER; adding :" << a.name() << ":" << a.value();
                    QDomElement e = m_document.createElement("property");
                    e.setAttribute("name", a.name());
                    QDomText value = m_document.createTextNode(a.value());
                    e.appendChild(value);
                    filt.appendChild(e);
                }
            }
        }*/

    // fix slowmotion
    QDomNodeList producers = westley.toElement().elementsByTagName("producer");
    max = producers.count();
    for (int i = 0; i < max; i++) {
        QDomElement prod = producers.at(i).toElement();
        if (prod.attribute("mlt_service") == "framebuffer") {
            QString slowmotionprod = prod.attribute("resource");
            slowmotionprod.replace(':', '?');
            kDebug() << "// FOUND WRONG SLOWMO, new: " << slowmotionprod;
            prod.setAttribute("resource", slowmotionprod);
        }
    }
    // move producers to correct place, markers to a global list, fix clip descriptions
    QDomElement markers = m_document.createElement("markers");
    producers = m_document.elementsByTagName("producer");
    max = producers.count();
    for (int i = 0; i < max; i++) {
        QDomElement prod = producers.at(0).toElement();
        QDomNode m = prod.firstChild();
        if (!m.isNull()) {
            if (m.toElement().tagName() == "markers") {
                QDomNodeList prodchilds = m.childNodes();
                int maxchild = prodchilds.count();
                for (int k = 0; k < maxchild; k++) {
                    QDomElement mark = prodchilds.at(0).toElement();
                    mark.setAttribute("id", prod.attribute("id"));
                    markers.insertAfter(mark, QDomNode());
                }
                prod.removeChild(m);
            } else if (prod.attribute("type").toInt() == TEXT) {
                // convert title clip
                if (m.toElement().tagName() == "textclip") {
                    QDomDocument tdoc;
                    QDomElement titleclip = m.toElement();
                    QDomElement title = tdoc.createElement("kdenlivetitle");
                    tdoc.appendChild(title);
                    QDomNodeList objects = titleclip.childNodes();
                    int maxchild = objects.count();
                    for (int k = 0; k < maxchild; k++) {
                        QString objectxml;
                        QDomElement ob = objects.at(k).toElement();
                        if (ob.attribute("type") == "3") {
                            // text object
                            QDomElement item = tdoc.createElement("item");
                            item.setAttribute("z-index", ob.attribute("z"));
                            item.setAttribute("type", "QGraphicsTextItem");
                            QDomElement position = tdoc.createElement("position");
                            position.setAttribute("x", ob.attribute("x"));
                            position.setAttribute("y", ob.attribute("y"));
                            QDomElement content = tdoc.createElement("content");
                            content.setAttribute("font", ob.attribute("font_family"));
                            content.setAttribute("font-size", ob.attribute("font_size"));
                            content.setAttribute("font-bold", ob.attribute("bold"));
                            content.setAttribute("font-italic", ob.attribute("italic"));
                            content.setAttribute("font-underline", ob.attribute("underline"));
                            QString col = ob.attribute("color");
                            QColor c(col);
                            content.setAttribute("font-color", colorToString(c));
                            QDomText conttxt = tdoc.createTextNode(ob.attribute("text"));
                            content.appendChild(conttxt);
                            item.appendChild(position);
                            item.appendChild(content);
                            title.appendChild(item);
                        } else if (ob.attribute("type") == "5") {
                            // rectangle object
                            QDomElement item = tdoc.createElement("item");
                            item.setAttribute("z-index", ob.attribute("z"));
                            item.setAttribute("type", "QGraphicsRectItem");
                            QDomElement position = tdoc.createElement("position");
                            position.setAttribute("x", ob.attribute("x"));
                            position.setAttribute("y", ob.attribute("y"));
                            QDomElement content = tdoc.createElement("content");
                            QString col = ob.attribute("color");
                            QColor c(col);
                            content.setAttribute("brushcolor", colorToString(c));
                            QString rect = "0,0,";
                            rect.append(ob.attribute("width"));
                            rect.append(",");
                            rect.append(ob.attribute("height"));
                            content.setAttribute("rect", rect);
                            item.appendChild(position);
                            item.appendChild(content);
                            title.appendChild(item);
                        }
                    }
                    prod.setAttribute("xmldata", tdoc.toString());
                    QStringList titleInfo = TitleWidget::getFreeTitleInfo(projectFolder());
                    prod.setAttribute("titlename", titleInfo.at(0));
                    prod.setAttribute("resource", titleInfo.at(1));
                    //kDebug()<<"TITLE DATA:\n"<<tdoc.toString();
                    prod.removeChild(m);
                }

            } else if (m.isText()) {
                QString comment = m.nodeValue();
                if (!comment.isEmpty()) {
                    prod.setAttribute("description", comment);
                }
                prod.removeChild(m);
            }
        }
        int duration = prod.attribute("duration").toInt();
        if (duration > 0) prod.setAttribute("out", QString::number(duration));
        westley.insertBefore(prod, QDomNode());

    }

    QDomNode westley0 = m_document.elementsByTagName("westley").at(0);
    if (!markers.firstChild().isNull()) westley0.appendChild(markers);
    westley0.removeChild(kdenlivedoc);

    QDomNodeList elements = westley.childNodes();
    max = elements.count();
    for (int i = 0; i < max; i++) {
        QDomElement prod = elements.at(0).toElement();
        westley0.insertAfter(prod, QDomNode());
    }

    westley0.removeChild(westley);

    //kDebug() << "/////////////////  CONVERTED DOC:";
    //kDebug() << m_document.toString();
    //kDebug() << "/////////////////  END CONVERTED DOC:";
}

QString KdenliveDoc::colorToString(const QColor& c) {
    QString ret = "%1,%2,%3,%4";
    ret = ret.arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    return ret;
}

void KdenliveDoc::saveSceneList(const QString &path, QDomDocument sceneList) {
    QDomNode wes = sceneList.elementsByTagName("westley").at(0);

    QDomElement addedXml = sceneList.createElement("kdenlivedoc");
    QDomElement markers = sceneList.createElement("markers");
    addedXml.setAttribute("version", "0.8");
    addedXml.setAttribute("profile", profilePath());
    addedXml.setAttribute("position", m_render->seekPosition().frames(m_fps));
    addedXml.setAttribute("zoom", m_zoom);

    QDomElement e;
    QList <DocClipBase*> list = m_clipManager->documentClipList();
    for (int i = 0; i < list.count(); i++) {
        e = list.at(i)->toXML();
        e.setTagName("kdenlive_producer");
        addedXml.appendChild(sceneList.importNode(e, true));
        QList < CommentedTime > marks = list.at(i)->commentedSnapMarkers();
        for (int j = 0; j < marks.count(); j++) {
            QDomElement marker = sceneList.createElement("marker");
            marker.setAttribute("time", marks.at(j).time().ms() / 1000);
            marker.setAttribute("comment", marks.at(j).comment());
            marker.setAttribute("id", e.attribute("id"));
            markers.appendChild(marker);
        }
    }
    addedXml.appendChild(markers);
    if (!m_guidesXml.isNull()) addedXml.appendChild(sceneList.importNode(m_guidesXml, true));

    wes.appendChild(addedXml);
    //wes.appendChild(doc.importNode(kdenliveData, true));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        kWarning() << "//////  ERROR writing to file: " << path;
        return;
    }
    QTextStream out(&file);
    out << sceneList.toString();
    file.close();

}

QDomElement KdenliveDoc::documentInfoXml() {
    QDomDocument doc;
    QDomElement e;
    QDomElement addedXml = doc.createElement("kdenlivedoc");
    QDomElement markers = doc.createElement("markers");
    addedXml.setAttribute("version", "0.7");
    addedXml.setAttribute("profile", profilePath());
    addedXml.setAttribute("position", m_render->seekPosition().frames(m_fps));
    addedXml.setAttribute("zoom", m_zoom);
    QList <DocClipBase*> list = m_clipManager->documentClipList();
    for (int i = 0; i < list.count(); i++) {
        e = list.at(i)->toXML();
        e.setTagName("kdenlive_producer");
        addedXml.appendChild(doc.importNode(e, true));
        QList < CommentedTime > marks = list.at(i)->commentedSnapMarkers();
        for (int j = 0; j < marks.count(); j++) {
            QDomElement marker = doc.createElement("marker");
            marker.setAttribute("time", marks.at(j).time().ms() / 1000);
            marker.setAttribute("comment", marks.at(j).comment());
            marker.setAttribute("id", e.attribute("id"));
            markers.appendChild(marker);
        }
    }
    addedXml.appendChild(markers);
    if (!m_guidesXml.isNull()) addedXml.appendChild(doc.importNode(m_guidesXml, true));
    //kDebug() << m_document.toString();
    return addedXml;
}


ClipManager *KdenliveDoc::clipManager() {
    return m_clipManager;
}

KUrl KdenliveDoc::projectFolder() const {
    if (m_projectFolder.isEmpty()) return KUrl(KStandardDirs::locateLocal("appdata", "/projects/"));
    return m_projectFolder;
}

QString KdenliveDoc::getDocumentStandard() {
    //WARNING: this way to tell the video standard is a bit hackish...
    if (m_profile.description.contains("pal", Qt::CaseInsensitive) || m_profile.description.contains("25", Qt::CaseInsensitive) || m_profile.description.contains("50", Qt::CaseInsensitive)) return "PAL";
    return "NTSC";
}

QString KdenliveDoc::profilePath() const {
    return m_profile.path;
}

MltVideoProfile KdenliveDoc::mltProfile() const {
    return m_profile;
}

void KdenliveDoc::setProfilePath(QString path) {
    if (path.isEmpty()) path = KdenliveSettings::default_profile();
    if (path.isEmpty()) path = "dv_pal";
    m_profile = ProfilesDialog::getVideoProfile(path);
    KdenliveSettings::setProject_display_ratio((double) m_profile.display_aspect_num / m_profile.display_aspect_den);
    m_fps = (double) m_profile.frame_rate_num / m_profile.frame_rate_den;
    m_width = m_profile.width;
    m_height = m_profile.height;
    kDebug() << "KDEnnlive document, init timecode from path: " << path << ",  " << m_fps;
    if (m_fps == 30000.0 / 1001.0) m_timecode.setFormat(30, true);
    else m_timecode.setFormat((int) m_fps);
}

const double KdenliveDoc::dar() {
    return (double) m_profile.display_aspect_num / m_profile.display_aspect_den;
}

void KdenliveDoc::setThumbsProgress(const QString &message, int progress) {
    emit progressInfo(message, progress);
}

void KdenliveDoc::loadingProgressed() {
    m_documentLoadingProgress += m_documentLoadingStep;
    emit progressInfo(QString(), (int) m_documentLoadingProgress);
}

QUndoStack *KdenliveDoc::commandStack() {
    return m_commandStack;
}

void KdenliveDoc::setRenderer(Render *render) {
    if (m_render) return;
    m_render = render;
    emit progressInfo(i18n("Loading playlist..."), 0);
    //qApp->processEvents();
    if (m_render) {
        m_render->setSceneList(m_document.toString(), m_startPos);
        checkProjectClips();
    }
    emit progressInfo(QString(), -1);
}

void KdenliveDoc::checkProjectClips() {
    if (m_render == NULL) return;
    QList <Mlt::Producer *> prods = m_render->producersList();
    QString id ;
    QString prodId ;
    QString prodTrack ;
    for (int i = 0; i < prods.count(); i++) {
        id = prods.at(i)->get("id");
        prodId = id.section('_', 0, 0);
        prodTrack = id.section('_', 1, 1);
        kDebug() << "CHECK PRO CLIP, ID: " << id;
        DocClipBase *clip = m_clipManager->getClipById(prodId);
        if (clip) clip->setProducer(prods.at(i));
        kDebug() << "CHECK PRO CLIP, ID: " << id << " DONE";
        if (clip && clip->clipType() == TEXT && !QFile::exists(clip->fileURL().path())) {
            // regenerate text clip image if required
            kDebug() << "// TITLE: " << clip->getProperty("titlename") << " Preview file: " << clip->getProperty("resource") << " DOES NOT EXIST";
            QString titlename = clip->getProperty("titlename");
            QString titleresource;
            if (titlename.isEmpty()) {
                QStringList titleInfo = TitleWidget::getFreeTitleInfo(projectFolder());
                titlename = titleInfo.at(0);
                titleresource = titleInfo.at(1);
                clip->setProperty("titlename", titlename);
                kDebug() << "// New title set to: " << titlename;
            } else {
                titleresource = TitleWidget::getTitleResourceFromName(projectFolder(), titlename);
            }
            QString titlepath = projectFolder().path() + "/titles/";
            TitleWidget *dia_ui = new TitleWidget(KUrl(), titlepath, m_render, kapp->activeWindow());
            QDomDocument doc;
            doc.setContent(clip->getProperty("xmldata"));
            dia_ui->setXml(doc);
            QPixmap pix = dia_ui->renderedPixmap();
            pix.save(titleresource);
            clip->setProperty("resource", titleresource);
            delete dia_ui;
            clip->producer()->set("force_reload", 1);
        }
    }
}

Render *KdenliveDoc::renderer() {
    return m_render;
}

void KdenliveDoc::updateClip(const QString &id) {
    emit updateClipDisplay(id);
}

int KdenliveDoc::getFramePos(QString duration) {
    return m_timecode.getFrameCount(duration, m_fps);
}

QString KdenliveDoc::producerName(const QString &id) {
    QString result = "unnamed";
    QDomNodeList prods = producersList();
    int ct = prods.count();
    for (int i = 0; i <  ct ; i++) {
        QDomElement e = prods.item(i).toElement();
        if (e.attribute("id") != "black" && e.attribute("id") == id) {
            result = e.attribute("name");
            if (result.isEmpty()) result = KUrl(e.attribute("resource")).fileName();
            break;
        }
    }
    return result;
}

void KdenliveDoc::setProducerDuration(const QString &id, int duration) {
    QDomNodeList prods = producersList();
    int ct = prods.count();
    for (int i = 0; i <  ct ; i++) {
        QDomElement e = prods.item(i).toElement();
        if (e.attribute("id") != "black" && e.attribute("id") == id) {
            e.setAttribute("duration", QString::number(duration));
            break;
        }
    }
}

int KdenliveDoc::getProducerDuration(const QString &id) {
    int result = 0;
    QDomNodeList prods = producersList();
    int ct = prods.count();
    for (int i = 0; i <  ct ; i++) {
        QDomElement e = prods.item(i).toElement();
        if (e.attribute("id") != "black" && e.attribute("id") == id) {
            result = e.attribute("duration").toInt();
            break;
        }
    }
    return result;
}


QDomDocument KdenliveDoc::generateSceneList() {
    QDomDocument doc;
    QDomElement westley = doc.createElement("westley");
    doc.appendChild(westley);
    QDomElement prod = doc.createElement("producer");
}

QDomDocument KdenliveDoc::toXml() const {
    return m_document;
}

Timecode KdenliveDoc::timecode() const {
    return m_timecode;
}

QDomNodeList KdenliveDoc::producersList() {
    return m_document.elementsByTagName("producer");
}

void KdenliveDoc::backupMltPlaylist() {
    if (m_render) m_scenelist = m_render->sceneList();
}

double KdenliveDoc::projectDuration() const {
    if (m_render) return GenTime(m_render->getLength(), m_fps).ms() / 1000;
}

double KdenliveDoc::fps() const {
    return m_fps;
}

int KdenliveDoc::width() const {
    return m_width;
}

int KdenliveDoc::height() const {
    return m_height;
}

KUrl KdenliveDoc::url() const {
    return m_url;
}

void KdenliveDoc::setUrl(KUrl url) {
    m_url = url;
}

void KdenliveDoc::setModified(bool mod) {
    if (!m_url.isEmpty() && mod && KdenliveSettings::crashrecovery()) {
        m_autoSaveTimer->start(3000);
    }
    if (mod == m_modified) return;
    m_modified = mod;
    emit docModified(m_modified);
}

bool KdenliveDoc::isModified() const {
    return m_modified;
}

QString KdenliveDoc::description() const {
    if (m_url.isEmpty())
        return i18n("Untitled") + " / " + m_profile.description;
    else
        return m_url.fileName() + " / " + m_profile.description;
}

void KdenliveDoc::addClip(QDomElement elem, QString clipId, bool createClipItem) {
    const QString producerId = clipId.section('_', 0, 0);
    int subtrack = clipId.section('_', 1, 1).toInt();
    DocClipBase *clip = m_clipManager->getClipById(producerId);
    if (clip == NULL) {
        elem.setAttribute("id", producerId);
        clip = new DocClipBase(m_clipManager, elem, producerId);
        m_clipManager->addClip(clip);
    }
    if (createClipItem) emit addProjectClip(clip);
}

void KdenliveDoc::addClipInfo(QDomElement elem, QString clipId) {
    DocClipBase *clip = m_clipManager->getClipById(clipId);
    if (clip == NULL) {
        addClip(elem, clipId);
    } else {
        QMap <QString, QString> properties;
        QDomNamedNodeMap attributes = elem.attributes();
        QString attrname;
        for (unsigned int i = 0; i < attributes.count(); i++) {
            attrname = attributes.item(i).nodeName();
            if (attrname != "resource")
                properties.insert(attrname, attributes.item(i).nodeValue());
            kDebug() << attrname << " = " << attributes.item(i).nodeValue();
        }
        clip->setProperties(properties);
        emit addProjectClip(clip);
    }
}

void KdenliveDoc::addFolder(const QString foldername, const QString &clipId, bool edit) {
    emit addProjectFolder(foldername, clipId, false, edit);
}

void KdenliveDoc::deleteFolder(const QString foldername, const QString &clipId) {
    emit addProjectFolder(foldername, clipId, true);
}

void KdenliveDoc::deleteProjectClip(QList <QString> ids) {
    for (int i = 0; i < ids.size(); ++i) {
        emit deleteTimelineClip(ids.at(i));
        m_clipManager->slotDeleteClip(ids.at(i));
    }
    setModified(true);
}

void KdenliveDoc::deleteProjectFolder(QMap <QString, QString> map) {
    QMapIterator<QString, QString> i(map);
    while (i.hasNext()) {
        i.next();
        slotDeleteFolder(i.key(), i.value());
    }
    setModified(true);
}

void KdenliveDoc::deleteClip(const QString &clipId) {
    emit signalDeleteProjectClip(clipId);
    m_clipManager->deleteClip(clipId);
}

void KdenliveDoc::slotAddClipList(const KUrl::List urls, const QString group, const QString &groupId) {
    m_clipManager->slotAddClipList(urls, group, groupId);
    emit selectLastAddedClip(QString::number(m_clipManager->lastClipId()));
    setModified(true);
}


void KdenliveDoc::slotAddClipFile(const KUrl url, const QString group, const QString &groupId) {
    kDebug() << "/////////  DOCUM, ADD CLP: " << url;
    m_clipManager->slotAddClipFile(url, group, groupId);
    emit selectLastAddedClip(QString::number(m_clipManager->lastClipId()));
    setModified(true);
}

void KdenliveDoc::slotAddFolder(const QString folderName) {
    AddFolderCommand *command = new AddFolderCommand(this, folderName, QString::number(m_clipManager->getFreeClipId()), true);
    commandStack()->push(command);
    setModified(true);
}

void KdenliveDoc::slotDeleteFolder(const QString folderName, const QString &id) {
    AddFolderCommand *command = new AddFolderCommand(this, folderName, id, false);
    commandStack()->push(command);
    setModified(true);
}

void KdenliveDoc::slotEditFolder(const QString newfolderName, const QString oldfolderName, const QString &clipId) {
    EditFolderCommand *command = new EditFolderCommand(this, newfolderName, oldfolderName, clipId, false);
    commandStack()->push(command);
    setModified(true);
}

const QString&KdenliveDoc::getFreeClipId() {
    return QString::number(m_clipManager->getFreeClipId());
}

DocClipBase *KdenliveDoc::getBaseClip(const QString &clipId) {
    return m_clipManager->getClipById(clipId);
}

void KdenliveDoc::slotAddColorClipFile(const QString name, const QString color, QString duration, const QString group, const QString &groupId) {
    m_clipManager->slotAddColorClipFile(name, color, duration, group, groupId);
    setModified(true);
}

void KdenliveDoc::slotAddSlideshowClipFile(const QString name, const QString path, int count, const QString duration, const bool loop, const bool fade, const QString &luma_duration, const QString &luma_file, const int softness, const QString group, const QString &groupId) {
    m_clipManager->slotAddSlideshowClipFile(name, path, count, duration, loop, fade, luma_duration, luma_file, softness, group, groupId);
    setModified(true);
}

void KdenliveDoc::slotCreateTextClip(QString group, const QString &groupId) {
    QString titlesFolder = projectFolder().path() + "/titles/";
    KStandardDirs::makeDir(titlesFolder);
    TitleWidget *dia_ui = new TitleWidget(KUrl(), titlesFolder, m_render, kapp->activeWindow());
    if (dia_ui->exec() == QDialog::Accepted) {
        QStringList titleInfo = TitleWidget::getFreeTitleInfo(projectFolder());
        QPixmap pix = dia_ui->renderedPixmap();
        pix.save(titleInfo.at(1));
        //dia_ui->saveTitle(path + ".kdenlivetitle");
        m_clipManager->slotAddTextClipFile(titleInfo.at(0), titleInfo.at(1), dia_ui->xml().toString(), QString(), QString());
        setModified(true);
    }
    delete dia_ui;
}



#include "kdenlivedoc.moc"

