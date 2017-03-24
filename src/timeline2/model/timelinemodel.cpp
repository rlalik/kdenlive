/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/


#include "timelinemodel.hpp"
#include "trackmodel.hpp"
#include "clipmodel.hpp"
#include "compositionmodel.hpp"
#include "groupsmodel.hpp"
#include "snapmodel.hpp"

#include "doc/docundostack.hpp"

#include <klocalizedstring.h>
#include <QDebug>
#include <QModelIndex>
#include <mlt++/MltTractor.h>
#include <mlt++/MltProfile.h>
#include <mlt++/MltTransition.h>
#include <mlt++/MltField.h>
#include <queue>
#ifdef LOGGING
#include <sstream>
#endif

#include "macros.hpp"

int TimelineModel::next_id = 0;



TimelineModel::TimelineModel(Mlt::Profile *profile, std::weak_ptr<DocUndoStack> undo_stack) :
    QAbstractItemModel(),
    m_tractor(new Mlt::Tractor(*profile)),
    m_snaps(new SnapModel()),
    m_undoStack(undo_stack),
    m_profile(profile),
    m_blackClip(new Mlt::Producer(*profile,"color:black")),
    m_lock(QReadWriteLock::Recursive)
{
    // Create black background track
    m_blackClip->set("id", "black_track");
    m_blackClip->set("mlt_type", "producer");
    m_blackClip->set("aspect_ratio", 1);
    m_blackClip->set("set.test_audio", 0);
    m_tractor->insert_track(*m_blackClip, 0);

#ifdef LOGGING
    m_logFile = std::ofstream("log.txt");
    m_logFile << "TEST_CASE(\"Regression\") {"<<std::endl;
    m_logFile << "Mlt::Profile profile;"<<std::endl;
    m_logFile << "std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);" <<std::endl;
    m_logFile << "std::shared_ptr<TimelineModel> timeline = TimelineItemModel::construct(new Mlt::Profile(), undoStack);" <<std::endl;
    m_logFile << "TimelineModel::next_id = 0;" <<std::endl;
    m_logFile << "int dummy_id;" <<std::endl;
#endif
}

TimelineModel::~TimelineModel()
{
    //Remove black background
    //m_tractor->remove_track(0);
    std::vector<int> all_ids;
    for(auto tracks : m_iteratorTable) {
        all_ids.push_back(tracks.first);
    }
    for(auto tracks : all_ids) {
        deregisterTrack_lambda(tracks, false)();
    }
}

int TimelineModel::getTracksCount() const
{
    READ_LOCK();
    int count = m_tractor->count();
    Q_ASSERT(count >= 0);
    // don't count the black background track
    Q_ASSERT(count -1 == static_cast<int>(m_allTracks.size()));
    return count - 1;
}

int TimelineModel::getClipsCount() const
{
    READ_LOCK();
    int size = int(m_allClips.size());
    return size;
}


int TimelineModel::getClipTrackId(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(m_allClips.count(clipId) > 0);
    const auto clip = m_allClips.at(clipId);
    int trackId = clip->getCurrentTrackId();
    return trackId;
}

int TimelineModel::getClipPosition(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(m_allClips.count(clipId) > 0);
    const auto clip = m_allClips.at(clipId);
    int pos = clip->getPosition();
    return pos;
}

int TimelineModel::getClipPlaytime(int clipId) const
{
    READ_LOCK();
    Q_ASSERT(m_allClips.count(clipId) > 0);
    const auto clip = m_allClips.at(clipId);
    int playtime = clip->getPlaytime();
    return playtime;
}

int TimelineModel::getTrackClipsCount(int trackId) const
{
    READ_LOCK();
    int count = getTrackById_const(trackId)->getClipsCount();
    return count;
}

int TimelineModel::getTrackPosition(int trackId) const
{
    READ_LOCK();
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    auto it = m_allTracks.begin();
    int pos = (int)std::distance(it, (decltype(it))m_iteratorTable.at(trackId));
    return pos;
}

bool TimelineModel::requestClipMove(int clipId, int trackId, int position, bool updateView, Fun &undo, Fun &redo)
{
    Q_ASSERT(isClip(clipId));
    std::function<bool (void)> local_undo = [](){return true;};
    std::function<bool (void)> local_redo = [](){return true;};
    bool ok = true;
    int old_trackId = getClipTrackId(clipId);
    if (old_trackId != -1) {
        ok = getTrackById(old_trackId)->requestClipDeletion(clipId, updateView, local_undo, local_redo);
        if (!ok) {
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    ok = getTrackById(trackId)->requestClipInsertion(clipId, position, updateView, local_undo, local_redo);
    if (!ok) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestClipMove(int clipId, int trackId, int position,  bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipMove("<<clipId<<","<<trackId<<" ,"<<position<<", "<<(updateView ? "true" : "false")<<", "<<(logUndo ? "true" : "false")<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_allClips.count(clipId) > 0);
    if (m_allClips[clipId]->getPosition() == position && getClipTrackId(clipId) == trackId) {
        return true;
    }
    if (m_groups->isInGroup(clipId)) {
        //element is in a group.
        int groupId = m_groups->getRootId(clipId);
        int current_trackId = getClipTrackId(clipId);
        int track_pos1 = getTrackPosition(trackId);
        int track_pos2 = getTrackPosition(current_trackId);
        int delta_track = track_pos1 - track_pos2;
        int delta_pos = position - m_allClips[clipId]->getPosition();
        return requestGroupMove(clipId, groupId, delta_track, delta_pos, updateView, logUndo);
    }
    std::function<bool (void)> undo = [](){return true;};
    std::function<bool (void)> redo = [](){return true;};
    bool res = requestClipMove(clipId, trackId, position, updateView, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move clip"));
    }
    return res;
}

int TimelineModel::suggestClipMove(int clipId, int trackId, int position)
{
#ifdef LOGGING
    m_logFile << "timeline->suggestClipMove("<<clipId<<","<<trackId<<" ,"<<position<<"); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(clipId));
    Q_ASSERT(isTrack(trackId));
    int currentPos = getClipPosition(clipId);
    int currentTrack = getClipTrackId(clipId);
    if (currentPos == position || currentTrack != trackId) {
        return position;
    }

    //For snapping, we must ignore all in/outs of the clips of the group being moved
    std::vector<int> ignored_pts;
    if (m_groups->isInGroup(clipId)) {
        int groupId = m_groups->getRootId(clipId);
        auto all_clips = m_groups->getLeaves(groupId);
        for (int current_clipId : all_clips) {
            int in = getClipPosition(current_clipId);
            int out = in + getClipPlaytime(current_clipId) - 1;
            ignored_pts.push_back(in);
            ignored_pts.push_back(out);
        }
    } else {
        int in = getClipPosition(clipId);
        int out = in + getClipPlaytime(clipId) - 1;
        ignored_pts.push_back(in);
        ignored_pts.push_back(out);
    }

    int snapped = requestBestSnapPos(position, m_allClips[clipId]->getPlaytime(), ignored_pts);
    qDebug() << "Starting suggestion "<<clipId << position << currentPos << "snapped to "<<snapped;
    if (snapped >= 0) {
        position = snapped;
    }
    //we check if move is possible
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool possible = requestClipMove(clipId, trackId, position, false, undo, redo);
    qDebug() << "Original move success" << possible;
    if (possible) {
        undo();
        return position;
    }
    bool after = position > currentPos;
    int blank_length = getTrackById(trackId)->getBlankSizeNearClip(clipId, after);
    qDebug() << "Found blank" << blank_length;
    if (blank_length < INT_MAX) {
        if (after) {
            return currentPos + blank_length;
        } else {
            return currentPos - blank_length;
        }
    }
    return position;
}

int TimelineModel::suggestCompositionMove(int compoId, int trackId, int position)
{
#ifdef LOGGING
    m_logFile << "timeline->suggestCompositionMove("<<compoId<<","<<trackId<<" ,"<<position<<"); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isComposition(compoId));
    Q_ASSERT(isTrack(trackId));
    int currentPos = getCompositionPosition(compoId);
    int currentTrack = getCompositionTrackId(compoId);
    if (currentPos == position || currentTrack != trackId) {
        return position;
    }

    //For snapping, we must ignore all in/outs of the clips of the group being moved
    std::vector<int> ignored_pts;
    if (m_groups->isInGroup(compoId)) {
        int groupId = m_groups->getRootId(compoId);
        auto all_clips = m_groups->getLeaves(groupId);
        for (int current_compoId : all_clips) {
            //TODO: fix for composition
            int in = getClipPosition(current_compoId);
            int out = in + getClipPlaytime(current_compoId) - 1;
            ignored_pts.push_back(in);
            ignored_pts.push_back(out);
        }
    } else {
        int in = currentPos;
        int out = in + getCompositionPlaytime(compoId) - 1;
        qDebug()<<" * ** IGNORING SNAP PTS: "<<in<<"-"<<out;
        ignored_pts.push_back(in);
        ignored_pts.push_back(out);
    }

    int snapped = requestBestSnapPos(position, m_allCompositions[compoId]->getPlaytime(), ignored_pts);
    qDebug() << "Starting suggestion "<<compoId << position << currentPos << "snapped to "<<snapped;
    if (snapped >= 0) {
        position = snapped;
    }
    //we check if move is possible
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool possible = requestCompositionMove(compoId, trackId, position, false, undo, redo);
    qDebug() << "Original move success" << possible;
    if (possible) {
        undo();
        return position;
    }
    bool after = position > currentPos;
    int blank_length = getTrackById(trackId)->getBlankSizeNearComposition(compoId, after);
    qDebug() << "Found blank" << blank_length;
    if (blank_length < INT_MAX) {
        if (after) {
            return currentPos + blank_length;
        } else {
            return currentPos - blank_length;
        }
    }
    return position;
}

bool TimelineModel::requestClipInsertion(std::shared_ptr<Mlt::Producer> prod, int trackId, int position, int &id, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "{" <<std::endl<< "std::shared_ptr<Mlt::Producer> producer = std::make_shared<Mlt::Producer>(profile, \"color\", \"red\");" << std::endl;
    m_logFile << "producer->set(\"length\", "<<prod->get_playtime()<<");" << std::endl;
    m_logFile << "producer->set(\"out\", "<<prod->get_playtime()-1<<");" << std::endl;
    m_logFile << "timeline->requestClipInsertion(producer,"<<trackId<<" ,"<<position<<", dummy_id );" <<std::endl;
    m_logFile<<"}"<<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestClipInsertion(prod, trackId, position, id, undo, redo);
    if (result && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Insert Clip"));
    }
    return result;
}

bool TimelineModel::requestClipInsertion(std::shared_ptr<Mlt::Producer> prod, int trackId, int position, int &id, Fun& undo, Fun& redo)
{
    int clipId = TimelineModel::getNextId();
    id = clipId;
    Fun local_undo = deregisterClip_lambda(clipId);
    ClipModel::construct(shared_from_this(), prod, clipId);
    auto clip = m_allClips[clipId];
    Fun local_redo = [clip, this](){
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is sufficient to register it.
        registerClip(clip);
        return true;
    };
    bool res = requestClipMove(clipId, trackId, position, true, local_undo, local_redo);
    if (!res) {
        Q_ASSERT(undo());
        id = -1;
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestClipDeletion(int clipId, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipDeletion("<<clipId<<"); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(clipId));
    if (m_groups->isInGroup(clipId)) {
        return requestGroupDeletion(clipId);
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool res = requestClipDeletion(clipId, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Delete Clip"));
    }
    return res;
}

bool TimelineModel::requestClipDeletion(int clipId, Fun& undo, Fun& redo)
{
    int trackId = getClipTrackId(clipId);
    if (trackId != -1) {
        bool res = getTrackById(trackId)->requestClipDeletion(clipId, true, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    auto operation = deregisterClip_lambda(clipId);
    auto clip = m_allClips[clipId];
    auto reverse = [this, clip]() {
        // We capture a shared_ptr to the clip, which means that as long as this undo object lives, the clip object is not deleted. To insert it back it is sufficient to register it.
        registerClip(clip);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        return true;
    }
    undo();
    return false;
}

bool TimelineModel::requestGroupMove(int clipId, int groupId, int delta_track, int delta_pos, bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestGroupMove("<<clipId<<","<<groupId<<" ,"<<delta_track<<", "<<delta_pos<<", "<<(updateView ? "true" : "false")<<", "<<(logUndo ? "true" : "false")<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    std::function<bool (void)> undo = [](){return true;};
    std::function<bool (void)> redo = [](){return true;};
    Q_ASSERT(m_allGroups.count(groupId) > 0);
    bool ok = true;
    auto all_clips = m_groups->getLeaves(groupId);
    std::vector<int> sorted_clips(all_clips.begin(), all_clips.end());
    //we have to sort clip in an order that allows to do the move without self conflicts
    //If we move up, we move first the clips on the upper tracks (and conversely).
    //If we move left, we move first the leftmost clips (and conversely).
    std::sort(sorted_clips.begin(), sorted_clips.end(), [delta_track, delta_pos, this](int clipId1, int clipId2){
            int trackId1 = getClipTrackId(clipId1);
            int trackId2 = getClipTrackId(clipId2);
            int track_pos1 = getTrackPosition(trackId1);
            int track_pos2 = getTrackPosition(trackId2);
            if (trackId1 == trackId2) {
                int p1 = m_allClips[clipId1]->getPosition();
                int p2 = m_allClips[clipId2]->getPosition();
                return !(p1 <= p2) == !(delta_pos <= 0);
            }
            return !(track_pos1 <= track_pos2) == !(delta_track <= 0);
        });
    for (int clip : sorted_clips) {
        int current_track_id = getClipTrackId(clip);
        int current_track_position = getTrackPosition(current_track_id);
        int target_track_position = current_track_position + delta_track;
        if (target_track_position >= 0 && target_track_position < getTracksCount()) {
            auto it = m_allTracks.cbegin();
            std::advance(it, target_track_position);
            int target_track = (*it)->getId();
            int target_position = m_allClips[clip]->getPosition() + delta_pos;
            ok = requestClipMove(clip, target_track, target_position, updateView || (clip != clipId), undo, redo);
        } else {
            ok = false;
        }
        if (!ok) {
            bool undone = undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    if (logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move group"));
    }
    return true;
}

bool TimelineModel::requestGroupDeletion(int clipId)
{
#ifdef LOGGING
    m_logFile << "timeline->requestGroupDeletion("<<clipId<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    // we do a breadth first exploration of the group tree, ungroup (delete) every inner node, and then delete all the leaves.
    std::queue<int> group_queue;
    group_queue.push(m_groups->getRootId(clipId));
    std::unordered_set<int> all_clips;
    while (!group_queue.empty()) {
        int current_group = group_queue.front();
        group_queue.pop();
        Q_ASSERT(isGroup(current_group));
        auto children = m_groups->getDirectChildren(current_group);
        int one_child = -1; //we need the id on any of the indices of the elements of the group
        for(int c : children) {
            if (isClip(c)) {
                all_clips.insert(c);
                one_child = c;
            } else {
                Q_ASSERT(isGroup(c));
                one_child = c;
                group_queue.push(c);
            }
        }
        if (one_child != -1) {
            bool res = m_groups->ungroupItem(one_child, undo, redo);
            if (!res) {
                undo();
                return false;
            }
        }
    }
    for(int clip : all_clips) {
        bool res = requestClipDeletion(clip, undo, redo);
        if (!res) {
            undo();
            return false;
        }
    }
    PUSH_UNDO(undo, redo, i18n("Remove group"));
    return true;
}


bool TimelineModel::requestClipResize(int clipId, int size, bool right, bool logUndo, bool snapping)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipResize("<<clipId<<","<<size<<" ,"<<(right ? "true" : "false")<<", "<<(logUndo ? "true" : "false")<<", "<<(snapping ? "true" : "false")<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isClip(clipId));
    if (snapping) {
        Fun temp_undo = [](){return true;};
        Fun temp_redo = [](){return true;};
        int in = getClipPosition(clipId);
        int out = in + getClipPlaytime(clipId) - 1;
        m_snaps->ignore({in,out});
        int proposed_size = -1;
        if (right) {
            int target_pos = in + size - 1;
            int snapped_pos = m_snaps->getClosestPoint(target_pos);
            //TODO Make the number of frames adjustable
            if (snapped_pos != -1 && qAbs(target_pos - snapped_pos) <= 10) {
                proposed_size = snapped_pos - in;
            }
        } else {
            int target_pos = out + 1 - size;
            int snapped_pos = m_snaps->getClosestPoint(target_pos);
            //TODO Make the number of frames adjustable
            if (snapped_pos != -1 && qAbs(target_pos - snapped_pos) <= 10) {
                proposed_size = out + 2 - snapped_pos;
            }
        }
        m_snaps->unIgnore();
        if (proposed_size != -1) {
            if (m_allClips[clipId]->requestResize(proposed_size, right, temp_undo, temp_redo)) {
                temp_undo(); //undo temp move
                size = proposed_size;
            }
        }
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    Fun update_model = [clipId, right, logUndo, this]() {
        if (getClipTrackId(clipId) != -1) {
            QModelIndex modelIndex = makeClipIndexFromID(clipId);
            if (right) {
                notifyChange(modelIndex, modelIndex, false, true, logUndo);
            } else {
                notifyChange(modelIndex, modelIndex, true, true, logUndo);
            }
        }
        return true;
    };
    bool result = m_allClips[clipId]->requestResize(size, right, undo, redo);
    if (result) {
        PUSH_LAMBDA(update_model, undo);
        PUSH_LAMBDA(update_model, redo);
        update_model();
        if (logUndo) {
            PUSH_UNDO(undo, redo, i18n("Resize clip"));
        }
    }
    return result;
}

bool TimelineModel::requestClipTrim(int clipId, int delta, bool right, bool ripple, bool logUndo)
{
    return requestClipResize(clipId, m_allClips[clipId]->getPlaytime() - delta, right, logUndo);
}

bool TimelineModel::requestClipsGroup(const std::unordered_set<int>& ids)
{
#ifdef LOGGING
    std::stringstream group;
    m_logFile << "{"<<std::endl;
    m_logFile << "auto group = {";
    bool deb = true;
    for (int clipId : ids) {
        if(deb) deb = false;
        else group << ", ";
        group<<clipId;
    }
    m_logFile << group.str() << "};" << std::endl;
    m_logFile <<"timeline->requestClipsGroup(group);" <<std::endl;
    m_logFile <<std::endl<< "}"<<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    for (int id : ids) {
        if (isClip(id)) {
            if (getClipTrackId(id) == -1) {
                return false;
            }
        } else if (!isGroup(id)) {
            return false;
        }
    }
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    int groupId = m_groups->groupItems(ids, undo, redo);
    if (groupId != -1) {
        PUSH_UNDO(undo, redo, i18n("Group clips"));
    }
    return (groupId != -1);
}

bool TimelineModel::requestClipUngroup(int id)
{
#ifdef LOGGING
    m_logFile << "timeline->requestClipUngroup("<<id<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestClipUngroup(id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Ungroup clips"));
    }
    return result;
}

bool TimelineModel::requestClipUngroup(int id, Fun& undo, Fun& redo)
{
    return m_groups->ungroupItem(id, undo, redo);
}

bool TimelineModel::requestTrackInsertion(int position, int &id)
{
#ifdef LOGGING
    m_logFile << "timeline->requestTrackInsertion("<<position<<", dummy_id ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestTrackInsertion(position, id, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Insert Track"));
    }
    return result;
}

bool TimelineModel::requestTrackInsertion(int position, int &id, Fun& undo, Fun& redo)
{
    if (position == -1) {
        position = (int)(m_allTracks.size());
    }
    if (position < 0 || position > (int)m_allTracks.size()) {
        return false;
    }
    int trackId = TimelineModel::getNextId();
    id = trackId;
    Fun local_undo = deregisterTrack_lambda(trackId);
    TrackModel::construct(shared_from_this(), trackId, position);
    auto track = getTrackById(trackId);
    Fun local_redo = [track, position, this](){
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is sufficient to register it.
        registerTrack(track, position);
        return true;
    };
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool TimelineModel::requestTrackDeletion(int trackId)
{
#ifdef LOGGING
    m_logFile << "timeline->requestTrackDeletion("<<trackId<<"); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestTrackDeletion(trackId, undo, redo);
    if (result) {
        PUSH_UNDO(undo, redo, i18n("Delete Track"));
    }
    return result;
}

bool TimelineModel::requestTrackDeletion(int trackId, Fun& undo, Fun& redo)
{
    Q_ASSERT(isTrack(trackId));
    std::vector<int> clips_to_delete;
    for(const auto& it : getTrackById(trackId)->m_allClips) {
        clips_to_delete.push_back(it.first);
    }
    Fun local_undo = [](){return true;};
    Fun local_redo = [](){return true;};
    for(int clip : clips_to_delete) {
        bool res = true;
        while (res && m_groups->isInGroup(clip)) {
            res = requestClipUngroup(clip, local_undo, local_redo);
        }
        if (res) {
            res = requestClipDeletion(clip, local_undo, local_redo);
        }
        if (!res) {
            bool u = local_undo();
            Q_ASSERT(u);
            return false;
        }
    }
    int old_position = getTrackPosition(trackId);
    auto operation = deregisterTrack_lambda(trackId, true);
    std::shared_ptr<TrackModel> track = getTrackById(trackId);
    auto reverse = [this, track, old_position]() {
        // We capture a shared_ptr to the track, which means that as long as this undo object lives, the track object is not deleted. To insert it back it is sufficient to register it.
        registerTrack(track, old_position);
        return true;
    };
    if (operation()) {
        UPDATE_UNDO_REDO(operation, reverse, local_undo, local_redo);
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
        return true;
    }
    local_undo();
    return false;

}

void TimelineModel::registerTrack(std::shared_ptr<TrackModel> track, int pos)
{
    int id = track->getId();
    if (pos == -1) {
        pos = static_cast<int>(m_allTracks.size());
    }
    Q_ASSERT(pos >= 0);
    Q_ASSERT(pos <= static_cast<int>(m_allTracks.size()));

    //effective insertion (MLT operation), add 1 to account for black background track
    int error = m_tractor->insert_track(*track ,pos + 1);
    Q_ASSERT(error == 0); //we might need better error handling...

    // we now insert in the list
    auto posIt = m_allTracks.begin();
    std::advance(posIt, pos);
    auto it = m_allTracks.insert(posIt, std::move(track));
    //it now contains the iterator to the inserted element, we store it
    Q_ASSERT(m_iteratorTable.count(id) == 0); //check that id is not used (shouldn't happen)
    m_iteratorTable[id] = it;
    _resetView();
}

void TimelineModel::registerClip(std::shared_ptr<ClipModel> clip)
{
    int id = clip->getId();
    Q_ASSERT(m_allClips.count(id) == 0);
    m_allClips[id] = clip;
    m_groups->createGroupItem(id);
}

void TimelineModel::registerGroup(int groupId)
{
    Q_ASSERT(m_allGroups.count(groupId) == 0);
    m_allGroups.insert(groupId);
}

Fun TimelineModel::deregisterTrack_lambda(int id, bool updateView)
{
    return [this, id, updateView]() {
        auto it = m_iteratorTable[id]; //iterator to the element
        int index = getTrackPosition(id); //compute index in list
        if (updateView) {
            QModelIndex root;
            _resetView();
        }
        m_tractor->remove_track(static_cast<int>(index + 1)); //melt operation, add 1 to account for black background track
        //send update to the model
        m_allTracks.erase(it);  //actual deletion of object
        m_iteratorTable.erase(id);  //clean table
        return true;
    };
}

Fun TimelineModel::deregisterClip_lambda(int clipId)
{
    return [this, clipId]() {
        Q_ASSERT(m_allClips.count(clipId) > 0);
        Q_ASSERT(getClipTrackId(clipId) == -1); //clip must be deleted from its track at this point
        Q_ASSERT(!m_groups->isInGroup(clipId)); //clip must be ungrouped at this point
        m_allClips.erase(clipId);
        m_groups->destructGroupItem(clipId);
        return true;
    };
}

void TimelineModel::deregisterGroup(int id)
{
    Q_ASSERT(m_allGroups.count(id) > 0);
    m_allGroups.erase(id);
}

std::shared_ptr<TrackModel> TimelineModel::getTrackById(int trackId)
{
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    return *m_iteratorTable[trackId];
}

const std::shared_ptr<TrackModel> TimelineModel::getTrackById_const(int trackId) const
{
    Q_ASSERT(m_iteratorTable.count(trackId) > 0);
    return *m_iteratorTable.at(trackId);
}

std::shared_ptr<ClipModel> TimelineModel::getClipPtr(int clipId) const
{
    Q_ASSERT(m_allClips.count(clipId) > 0);
    return m_allClips.at(clipId);
}

std::shared_ptr<CompositionModel> TimelineModel::getCompositionPtr(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    return m_allCompositions.at(compoId);
}

int TimelineModel::getNextId()
{
    return TimelineModel::next_id++;
}

bool TimelineModel::isClip(int id) const
{
    return m_allClips.count(id) > 0;
}

bool TimelineModel::isComposition(int id) const
{
    return m_allCompositions.count(id) > 0;
}

bool TimelineModel::isTrack(int id) const
{
    return m_iteratorTable.count(id) > 0;
}

bool TimelineModel::isGroup(int id) const
{
    return m_allGroups.count(id) > 0;
}

int TimelineModel::duration() const
{
    return m_tractor->get_playtime();
}


std::unordered_set<int> TimelineModel::getGroupElements(int clipId)
{
    int groupId = m_groups->getRootId(clipId);
    return m_groups->getLeaves(groupId);
}

Mlt::Profile *TimelineModel::getProfile()
{
    return m_profile;
}

bool TimelineModel::requestReset(Fun& undo, Fun& redo)
{
    std::vector<int> all_ids;
    for (const auto& track : m_iteratorTable) {
        all_ids.push_back(track.first);
    }
    bool ok = true;
    for (int trackId : all_ids) {
        ok = ok && requestTrackDeletion(trackId, undo, redo);
    }
    return ok;
}

void TimelineModel::setUndoStack(std::weak_ptr<DocUndoStack> undo_stack)
{
    m_undoStack = undo_stack;
}

int TimelineModel::requestBestSnapPos(int pos, int length, const std::vector<int>& pts)
{
    if (pts.size() > 0) {
        m_snaps->ignore(pts);
    }
    int snapped_start = m_snaps->getClosestPoint(pos);
    qDebug() << "snapping start suggestion" <<snapped_start;
    int snapped_end = m_snaps->getClosestPoint(pos + length);
    m_snaps->unIgnore();
    
    int startDiff = qAbs(pos - snapped_start);
    int endDiff = qAbs(pos + length - snapped_end);
    if (startDiff < endDiff && snapped_start >= 0) {
        // snap to start
        if (startDiff < 10) {
            return snapped_start;
        }
    } else {
        // snap to end
        if (endDiff < 10 && snapped_end >= 0) {
            return snapped_end - length;
        }
    }
    return -1;
}

int TimelineModel::requestNextSnapPos(int pos)
{
    return m_snaps->getNextPoint(pos);
}

int TimelineModel::requestPreviousSnapPos(int pos)
{
    return m_snaps->getPreviousPoint(pos);
}

void TimelineModel::registerComposition(std::shared_ptr<CompositionModel> composition)
{
    int id = composition->getId();
    Q_ASSERT(m_allCompositions.count(id) == 0);
    m_allCompositions[id] = composition;
    m_groups->createGroupItem(id);
}

bool TimelineModel::requestCompositionInsertion(const QString& transitionId, int trackId, int position, int &id, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestCompositionInsertion(\"composite\","<<trackId<<" ,"<<position<<", dummy_id );" <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Fun undo = [](){return true;};
    Fun redo = [](){return true;};
    bool result = requestCompositionInsertion(transitionId, trackId, position, id, undo, redo);
    if (result && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Insert Composition"));
    }
    return result;
}

bool TimelineModel::requestCompositionInsertion(const QString& transitionId, int trackId, int position, int &id, Fun& undo, Fun& redo)
{
    int compositionId = TimelineModel::getNextId();
    id = compositionId;
    Fun local_undo = deregisterComposition_lambda(compositionId);
    CompositionModel::construct(shared_from_this(), transitionId, compositionId);
    auto composition = m_allCompositions[compositionId];
    Fun local_redo = [composition, this](){
        // We capture a shared_ptr to the composition, which means that as long as this undo object lives, the composition object is not deleted. To insert it back it is sufficient to register it.
        registerComposition(composition);
        return true;
    };
    bool res = requestCompositionMove(compositionId, trackId, position, true, local_undo, local_redo);
    if (!res) {
        Q_ASSERT(undo());
        id = -1;
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

Fun TimelineModel::deregisterComposition_lambda(int compoId)
{
    return [this, compoId]() {
        Q_ASSERT(m_allCompositions.count(compoId) > 0);
        Q_ASSERT(!m_groups->isInGroup(compoId)); //composition must be ungrouped at this point
        m_allCompositions.erase(compoId);
        m_groups->destructGroupItem(compoId);
        return true;
    };
}

int TimelineModel::getCompositionTrackId(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    return trans->getCurrentTrackId();
}

int TimelineModel::getCompositionPosition(int compoId) const
{
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    return trans->getPosition();
}

int TimelineModel::getCompositionPlaytime(int compoId) const
{
    READ_LOCK();
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    const auto trans = m_allCompositions.at(compoId);
    int playtime = trans->getPlaytime();
    return playtime;
}

int TimelineModel::getTrackCompositionsCount(int compoId) const
{
    return getTrackById_const(compoId)->getCompositionsCount();
}

bool TimelineModel::requestCompositionMove(int compoId, int trackId, int position,  bool updateView, bool logUndo)
{
#ifdef LOGGING
    m_logFile << "timeline->requestCompositionMove("<<compoId<<","<<trackId<<" ,"<<position<<", "<<(updateView ? "true" : "false")<<", "<<(logUndo ? "true" : "false")<<" ); " <<std::endl;
#endif
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_allCompositions.count(compoId) > 0);
    if (m_allCompositions[compoId]->getPosition() == position && getCompositionTrackId(compoId) == trackId) {
        return true;
    }
    if (m_groups->isInGroup(compoId)) {
        //element is in a group.
        int groupId = m_groups->getRootId(compoId);
        int current_trackId = getCompositionTrackId(compoId);
        int track_pos1 = getTrackPosition(trackId);
        int track_pos2 = getTrackPosition(current_trackId);
        int delta_track = track_pos1 - track_pos2;
        int delta_pos = position - m_allCompositions[compoId]->getPosition();
        return requestGroupMove(compoId, groupId, delta_track, delta_pos, updateView, logUndo);
    }
    std::function<bool (void)> undo = [](){return true;};
    std::function<bool (void)> redo = [](){return true;};
    bool res = requestCompositionMove(compoId, trackId, position, updateView, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move composition"));
    }
    return res;
}


bool TimelineModel::requestCompositionMove(int compoId, int trackId, int position, bool updateView, Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(isComposition(compoId));
    std::function<bool (void)> local_undo = [](){return true;};
    std::function<bool (void)> local_redo = [](){return true;};
    bool ok = true;
    int old_trackId = getCompositionTrackId(compoId);
    if (old_trackId != -1) {
        if (old_trackId == trackId) {
            // Simply setting in/out is enough
            local_undo = getTrackById(old_trackId)->requestCompositionResize_lambda(compoId, position);
            if (!ok) {
                qDebug()<<"------------\nFAILED TO RESIZE TRANS: "<<old_trackId;
                bool undone = local_undo();
                Q_ASSERT(undone);
                return false;
            }
            UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
            return true;
        }
        ok = getTrackById(old_trackId)->requestCompositionDeletion(compoId, updateView, local_undo, local_redo);
        if (!ok) {
            qDebug()<<"------------\nFAILED TO DELETE TRANS: "<<old_trackId;
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    ok = getTrackById(trackId)->requestCompositionInsertion(compoId, position, updateView, local_undo, local_redo);
    if (!ok) {
        bool undone = local_undo();
        Q_ASSERT(undone);
        return false;
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

void TimelineModel::plantComposition(Mlt::Transition &tr, int a_track, int b_track)
{
    //qDebug()<<"* * PLANT COMPOSITION: "<<tr.get("mlt_service")<<", TRACK: "<<a_track<<"x"<<b_track<<" ON POS: "<<tr.get_in();
    QScopedPointer<Mlt::Field> field(m_tractor->field());
    mlt_service nextservice = mlt_service_get_producer(field.data()->get_service());
    mlt_properties properties = MLT_SERVICE_PROPERTIES(nextservice);
    QString resource = mlt_properties_get(properties, "mlt_service");
    QList<Mlt::Transition *> trList;
    mlt_properties insertproperties = tr.get_properties();
    QString insertresource = mlt_properties_get(insertproperties, "mlt_service");
    bool isMixComposition = insertresource == QLatin1String("mix");

    mlt_service_type mlt_type = mlt_service_identify(nextservice);
    while (mlt_type == transition_type) {
        Mlt::Transition composition((mlt_transition) nextservice);
        nextservice = mlt_service_producer(nextservice);
        int aTrack = composition.get_a_track();
        int bTrack = composition.get_b_track();
        int internal = composition.get_int("internal_added");
        if ((isMixComposition || resource != QLatin1String("mix")) && (internal > 0 || aTrack < a_track || (aTrack == a_track && bTrack > b_track))) {
            Mlt::Properties trans_props(composition.get_properties());
            Mlt::Transition *cp = new Mlt::Transition(*m_tractor->profile(), composition.get("mlt_service"));
            Mlt::Properties new_trans_props(cp->get_properties());
            //new_trans_props.inherit(trans_props);
            new_trans_props.inherit(trans_props);
            trList.append(cp);
            field->disconnect_service(composition);
        }
        //else qCDebug(KDENLIVE_LOG) << "// FOUND TRANS OK, "<<resource<< ", A_: " << aTrack << ", B_ "<<bTrack;

        if (nextservice == nullptr) {
            break;
        }
        properties = MLT_SERVICE_PROPERTIES(nextservice);
        mlt_type = mlt_service_identify(nextservice);
        resource = mlt_properties_get(properties, "mlt_service");
    }
    field->plant_transition(tr, a_track, b_track);

    // re-add upper compositions
    for (int i = trList.count() - 1; i >= 0; --i) {
        ////qCDebug(KDENLIVE_LOG)<< "REPLANT ON TK: "<<trList.at(i)->get_a_track()<<", "<<trList.at(i)->get_b_track();
        field->plant_transition(*trList.at(i), trList.at(i)->get_a_track(), trList.at(i)->get_b_track());
    }
    qDeleteAll(trList);
}

bool TimelineModel::removeComposition(int compoId, int pos)
{
    //qDebug()<<"* * * TRYING TO DELETE COMPOSITION: "<<compoId<<" / "<<pos;
    QScopedPointer<Mlt::Field> field(m_tractor->field());
    field->lock();
    mlt_service nextservice = mlt_service_get_producer(field->get_service());
    mlt_properties properties = MLT_SERVICE_PROPERTIES(nextservice);
    QString resource = mlt_properties_get(properties, "mlt_service");
    bool found = false;
    ////qCDebug(KDENLIVE_LOG) << " del trans pos: " << in.frames(25) << '-' << out.frames(25);

    mlt_service_type mlt_type = mlt_service_identify(nextservice);
    while (mlt_type == transition_type) {
        mlt_transition tr = (mlt_transition) nextservice;
        int currentTrack = mlt_transition_get_b_track(tr);
        int currentATrack = mlt_transition_get_a_track(tr);
        int currentIn = (int) mlt_transition_get_in(tr);
        int currentOut = (int) mlt_transition_get_out(tr);
        //qDebug() << "// FOUND EXISTING TRANS, IN: " << currentIn << ", OUT: " << currentOut << ", TRACK: " << currentTrack<<" / "<<currentATrack;

        if (compoId == currentTrack && currentIn == pos) {
            found = true;
            mlt_field_disconnect_service(field->get_field(), nextservice);
            break;
        }
        nextservice = mlt_service_producer(nextservice);
        if (nextservice == nullptr) {
            break;
        }
        properties = MLT_SERVICE_PROPERTIES(nextservice);
        mlt_type = mlt_service_identify(nextservice);
        resource = mlt_properties_get(properties, "mlt_service");
    }
    field->unlock();
    return found;
}
