// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/accounts/AccountModel.hpp"

#include "controllers/accounts/Account.hpp"
#include "util/StandardItemHelper.hpp"

namespace chatterino {

AccountModel::AccountModel(QObject *parent)
    : SignalVectorModel<std::shared_ptr<Account>>(1, parent)
{
}

// turn a vector item into a model row
std::shared_ptr<Account> AccountModel::getItemFromRow(
    std::vector<QStandardItem *> &, const std::shared_ptr<Account> &original,
    std::optional<QModelIndex> index, std::optional<int> role)
{
    return original;
}

// turns a row in the model into a vector item
void AccountModel::getRowFromItem(const std::shared_ptr<Account> &item,
                                  std::vector<QStandardItem *> &row)
{
    setStringItem(row[0], item->toString(), false);
    row[0]->setData(QFont("Segoe UI", 10), Qt::FontRole);
}

int AccountModel::beforeInsert(const std::shared_ptr<Account> &item,
                               std::vector<QStandardItem *> &row,
                               int /*proposedIndex*/)
{
    // The index SignalVectorModel proposes counts the category headers as if
    // they were items of the vector. When the new account is the last of its
    // category, it comes out one header too far, so the account (or its new
    // header) ended up under the next category (a Kick account under "Dgg", a
    // Dgg account under "Twitch"). So the place is worked out from the rows
    // instead: categories are in order, and so are the accounts in each.
    const auto &rows = this->rows();
    const int count = static_cast<int>(rows.size());
    const QString &category = item->getCategory();

    int headerIndex = -1;    // the header of this category, if it has one
    int nextHeader = count;  // the header of the next category, or the end
    for (int i = 0; i < count; i++)
    {
        if (!rows[i].isCustomRow)
        {
            continue;
        }

        const QString name = rows[i].items[0]->text();
        if (name == category)
        {
            headerIndex = i;
            continue;
        }
        if (headerIndex >= 0 || name > category)
        {
            nextHeader = i;
            break;
        }
    }

    if (headerIndex < 0)
    {
        // the first account of its category: the header goes before the
        // header of the next category
        this->categoryCount_[category] = 1;

        auto newRow = this->createRow();

        setStringItem(newRow[0], category, false, false);
        newRow[0]->setData(QFont("Segoe UI Light", 16), Qt::FontRole);

        this->insertCustomRow(std::move(newRow), nextHeader);

        return nextHeader + 1;
    }

    this->categoryCount_[category]++;

    // before the first account of the category that doesn't sort before it
    for (int i = headerIndex + 1; i < nextHeader; i++)
    {
        const auto &original = rows[i].original;
        if (original && !(*original)->operator<(*item))
        {
            return i;
        }
    }
    return nextHeader;
}

void AccountModel::afterRemoved(const std::shared_ptr<Account> &item,
                                std::vector<QStandardItem *> &row, int index)
{
    auto it = this->categoryCount_.find(item->getCategory());
    assert(it != this->categoryCount_.end());

    if (it->second <= 1)
    {
        this->categoryCount_.erase(it);
        this->removeCustomRow(index - 1);
    }
    else
    {
        it->second--;
    }
}

}  // namespace chatterino
