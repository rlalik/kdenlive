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

#ifndef TRANSITIONTREEMODEL_H
#define TRANSITIONTREEMODEL_H

#include "abstractmodel/abstracttreemodel.hpp"
#include "assets/assetlist/model/assettreemodel.hpp"

/* @brief This class represents a transition hierarchy to be displayed as a tree
 */
class TreeItem;
class TransitionTreeModel : public AssetTreeModel
{

protected:
    explicit TransitionTreeModel(QObject *parent);

public:
    // if flat = true, then the categories are not created
    static std::shared_ptr<TransitionTreeModel> construct(bool flat = false, QObject *parent = nullptr);
    void reloadAssetMenu(QMenu *effectsMenu, KActionCategory *effectActions) override;
    void setFavorite(const QModelIndex &index, bool favorite, bool isEffect) override;
    void deleteEffect(const QModelIndex &index) override;
    void editCustomAsset(const QString newName, const QString newDescription, const QModelIndex &index) override;
protected:
};

#endif
