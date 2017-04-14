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

#include "groupsmodel.hpp"
#include "timelineitemmodel.hpp"
#include <QDebug>
#include <QModelIndex>
#include <queue>

GroupsModel::GroupsModel(std::weak_ptr<TimelineItemModel> parent) : m_parent(parent)
{
}

Fun GroupsModel::groupItems_lambda(int gid, const std::unordered_set<int> &ids)
{
    return [gid, ids, this]() {
        createGroupItem(gid);

        Q_ASSERT(m_groupIds.count(gid) == 0);
        m_groupIds.insert(gid);

        auto ptr = m_parent.lock();
        if (ptr) {
            ptr->registerGroup(gid);
        } else {
            qDebug() << "Impossible to create group because the timeline is not available anymore";
            Q_ASSERT(false);
        }
        std::unordered_set<int> roots;
        std::transform(ids.begin(), ids.end(), std::inserter(roots, roots.begin()), [&](int id) { return getRootId(id); });
        for (int id : roots) {
            setGroup(getRootId(id), gid);
            if (ptr->isClip(id)) {
                QModelIndex ix = ptr->makeClipIndexFromID(id);
                ptr->dataChanged(ix, ix, {TimelineItemModel::GroupedRole});
            }
        }
        return true;
    };
}

int GroupsModel::groupItems(const std::unordered_set<int> &ids, Fun &undo, Fun &redo)
{
    Q_ASSERT(!ids.empty());
    if (ids.size() == 1) {
        // We do not create a group with only one element. Instead, we return the id of that element
        return *(ids.begin());
    }
    int gid = TimelineModel::getNextId();
    auto operation = groupItems_lambda(gid, ids);
    if (operation()) {
        auto reverse = destructGroupItem_lambda(gid);
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        return gid;
    }
    return -1;
}

bool GroupsModel::ungroupItem(int id, Fun &undo, Fun &redo)
{
    int gid = getRootId(id);
    if (m_groupIds.count(gid) == 0) {
        // element is not part of a group
        return false;
    }

    return destructGroupItem(gid, true, undo, redo);
}

void GroupsModel::createGroupItem(int id)
{
    Q_ASSERT(m_upLink.count(id) == 0);
    Q_ASSERT(m_downLink.count(id) == 0);
    m_upLink[id] = -1;
    m_downLink[id] = std::unordered_set<int>();
}

Fun GroupsModel::destructGroupItem_lambda(int id)
{
    return [this, id]() {
        auto ptr = m_parent.lock();
        if (m_groupIds.count(id) > 0) {
            if (ptr) {
                ptr->deregisterGroup(id);
                m_groupIds.erase(id);
            } else {
                qDebug() << "Impossible to ungroup item because the timeline is not available anymore";
                Q_ASSERT(false);
            }
        }
        removeFromGroup(id);
        for (int child : m_downLink[id]) {
            m_upLink[child] = -1;
            if (ptr->isClip(child)) {
                QModelIndex ix = ptr->makeClipIndexFromID(child);
                ptr->dataChanged(ix, ix, {TimelineItemModel::GroupedRole});
            }
        }
        m_downLink.erase(id);
        m_upLink.erase(id);
        return true;
    };
}

bool GroupsModel::destructGroupItem(int id, bool deleteOrphan, Fun &undo, Fun &redo)
{
    Q_ASSERT(m_upLink.count(id) > 0);
    int parent = m_upLink[id];
    auto old_children = m_downLink[id];
    auto operation = destructGroupItem_lambda(id);
    if (operation()) {
        auto reverse = groupItems_lambda(id, old_children);
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
        if (parent != -1 && m_downLink[parent].empty() && deleteOrphan) {
            return destructGroupItem(parent, true, undo, redo);
        }
        return true;
    }
    return false;
}

bool GroupsModel::destructGroupItem(int id)
{
    return destructGroupItem_lambda(id)();
}

int GroupsModel::getRootId(int id) const
{
    std::unordered_set<int> seen; // we store visited ids to detect cycles
    int father = -1;
    do {
        Q_ASSERT(m_upLink.count(id) > 0);
        Q_ASSERT(seen.count(id) == 0);
        seen.insert(id);
        father = m_upLink.at(id);
        if (father != -1) {
            id = father;
        }
    } while (father != -1);
    return id;
}

bool GroupsModel::isLeaf(int id) const
{
    Q_ASSERT(m_downLink.count(id) > 0);
    return m_downLink.at(id).empty();
}

bool GroupsModel::isInGroup(int id) const
{
    Q_ASSERT(m_downLink.count(id) > 0);
    return getRootId(id) != id;
}

std::unordered_set<int> GroupsModel::getSubtree(int id) const
{
    std::unordered_set<int> result;
    result.insert(id);
    std::queue<int> queue;
    queue.push(id);
    while (!queue.empty()) {
        int current = queue.front();
        queue.pop();
        for (const int &child : m_downLink.at(current)) {
            result.insert(child);
            queue.push(child);
        }
    }
    return result;
}

std::unordered_set<int> GroupsModel::getLeaves(int id) const
{
    std::unordered_set<int> result;
    std::queue<int> queue;
    queue.push(id);
    while (!queue.empty()) {
        int current = queue.front();
        queue.pop();
        for (const int &child : m_downLink.at(current)) {
            queue.push(child);
        }
        if (m_downLink.at(current).empty()) {
            result.insert(current);
        }
    }
    return result;
}

std::unordered_set<int> GroupsModel::getDirectChildren(int id) const
{
    Q_ASSERT(m_downLink.count(id) > 0);
    return m_downLink.at(id);
}

void GroupsModel::setGroup(int id, int groupId)
{
    Q_ASSERT(m_upLink.count(id) > 0);
    Q_ASSERT(m_downLink.count(groupId) > 0);
    Q_ASSERT(id != groupId);
    removeFromGroup(id);
    m_upLink[id] = groupId;
    m_downLink[groupId].insert(id);
}

void GroupsModel::removeFromGroup(int id)
{
    Q_ASSERT(m_upLink.count(id) > 0);
    Q_ASSERT(m_downLink.count(id) > 0);
    int parent = m_upLink[id];
    if (parent != -1) {
        m_downLink[parent].erase(id);
    }
    m_upLink[id] = -1;
}
