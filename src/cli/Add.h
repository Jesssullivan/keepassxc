/*
 *  Copyright (C) 2019 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KEEPASSXC_ADD_H
#define KEEPASSXC_ADD_H

#include "DatabaseCommand.h"

class Entry;

class Add : public DatabaseCommand
{
public:
    Add();

    int executeWithDatabase(QSharedPointer<Database> db, QSharedPointer<QCommandLineParser> parser) override;

    static const QCommandLineOption UsernameOption;
    static const QCommandLineOption UrlOption;
    static const QCommandLineOption NotesOption;
    static const QCommandLineOption PasswordPromptOption;
    static const QCommandLineOption GenerateOption;
    static const QCommandLineOption AttributeOption;
    static const QCommandLineOption AttributePromptOption;
    static const QCommandLineOption ProtectOption;

    static bool validateAttributeOptions(QSharedPointer<QCommandLineParser> parser,
                                         Entry* entry = nullptr,
                                         const QStringList& unprotectedNames = {});
    static bool readAttributeValues(QSharedPointer<QCommandLineParser> parser, QList<QPair<QString, QString>>& values);
    static void setAttributes(Entry* entry,
                              QSharedPointer<QCommandLineParser> parser,
                              const QList<QPair<QString, QString>>& values,
                              const QStringList& unprotectedNames = {});
};

#endif // KEEPASSXC_ADD_H
