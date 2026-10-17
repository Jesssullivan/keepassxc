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

#include "Add.h"

#include "Generate.h"
#include "Utils.h"
#include "core/Global.h"
#include "core/Group.h"
#include "core/PasswordGenerator.h"

#include <QCommandLineParser>

const QCommandLineOption Add::UsernameOption = QCommandLineOption(QStringList() << "u" << "username",
                                                                  QObject::tr("Username for the entry."),
                                                                  QObject::tr("username"));

const QCommandLineOption Add::UrlOption =
    QCommandLineOption(QStringList() << "url", QObject::tr("URL for the entry."), QObject::tr("URL"));

const QCommandLineOption Add::NotesOption =
    QCommandLineOption(QStringList() << "notes", QObject::tr("Notes for the entry."), QObject::tr("Notes"));

const QCommandLineOption Add::PasswordPromptOption =
    QCommandLineOption(QStringList() << "p" << "password-prompt", QObject::tr("Prompt for the entry's password."));

const QCommandLineOption Add::GenerateOption =
    QCommandLineOption(QStringList() << "g" << "generate", QObject::tr("Generate a password for the entry."));

const QCommandLineOption Add::AttributeOption =
    QCommandLineOption(QStringList() << "a" << "attribute",
                       QObject::tr("Set a custom attribute of the entry. This option can be specified more than once."),
                       QObject::tr("attribute=value"));

const QCommandLineOption Add::AttributePromptOption = QCommandLineOption(
    QStringList() << "A" << "attribute-prompt",
    QObject::tr(
        "Prompt for the value of a custom attribute of the entry. This option can be specified more than once."),
    QObject::tr("attribute"));

const QCommandLineOption Add::ProtectOption = QCommandLineOption(
    QStringList() << "protect",
    QObject::tr("Protect a custom attribute of the entry. This option can be specified more than once."),
    QObject::tr("attribute"));

Add::Add()
{
    name = QString("add");
    description = QObject::tr("Add a new entry to a database.");
    options.append(Add::UsernameOption);
    options.append(Add::UrlOption);
    options.append(Add::NotesOption);
    options.append(Add::PasswordPromptOption);
    options.append(Add::AttributeOption);
    options.append(Add::AttributePromptOption);
    options.append(Add::ProtectOption);
    positionalArguments.append({QString("entry"), QObject::tr("Path of the entry to add."), QString("")});

    // Password generation options.
    options.append(Add::GenerateOption);
    options.append(Generate::PasswordLengthOption);
    options.append(Generate::LowerCaseOption);
    options.append(Generate::UpperCaseOption);
    options.append(Generate::NumbersOption);
    options.append(Generate::SpecialCharsOption);
    options.append(Generate::ExtendedAsciiOption);
    options.append(Generate::ExcludeCharsOption);
    options.append(Generate::ExcludeSimilarCharsOption);
    options.append(Generate::IncludeEveryGroupOption);
    options.append(Generate::CustomCharacterSetOption);
}

int Add::executeWithDatabase(QSharedPointer<Database> database, QSharedPointer<QCommandLineParser> parser)
{
    auto& out = Utils::STDOUT;
    auto& err = Utils::STDERR;

    const QStringList args = parser->positionalArguments();
    auto& entryPath = args.at(1);

    // Cannot use those 2 options at the same time!
    if (parser->isSet(Add::GenerateOption) && parser->isSet(Add::PasswordPromptOption)) {
        err << QObject::tr("Cannot generate a password and prompt at the same time.") << Qt::endl;
        return EXIT_FAILURE;
    }

    // Validating the password generator here, before we actually create
    // the entry.
    QSharedPointer<PasswordGenerator> passwordGenerator;
    if (parser->isSet(Add::GenerateOption)) {
        passwordGenerator = Generate::createGenerator(parser);
        if (passwordGenerator.isNull()) {
            return EXIT_FAILURE;
        }
    }

    if (!Add::validateAttributeOptions(parser)) {
        return EXIT_FAILURE;
    }

    Entry* entry = database->rootGroup()->addEntryWithPath(entryPath);
    if (!entry) {
        err << QObject::tr("Could not create entry with path %1.").arg(entryPath) << Qt::endl;
        return EXIT_FAILURE;
    }

    if (!parser->value(Add::UsernameOption).isEmpty()) {
        entry->setUsername(parser->value(Add::UsernameOption));
    }

    if (!parser->value(Add::UrlOption).isEmpty()) {
        entry->setUrl(parser->value(Add::UrlOption));
    }

    if (!parser->value(Add::NotesOption).isEmpty()) {
        entry->setNotes(parser->value(Add::NotesOption).replace("\\n", "\n"));
    }

    if (parser->isSet(Add::PasswordPromptOption)) {
        if (!parser->isSet(Command::QuietOption)) {
            out << QObject::tr("Enter password for new entry: ") << Qt::flush;
        }
        QString password = Utils::getPassword(parser->isSet(Command::QuietOption));
        entry->setPassword(password);
    } else if (parser->isSet(Add::GenerateOption)) {
        QString password = passwordGenerator->generatePassword();
        entry->setPassword(password);
    }

    QList<QPair<QString, QString>> attributeValues;
    if (!Add::readAttributeValues(parser, attributeValues)) {
        delete entry;
        return EXIT_FAILURE;
    }
    Add::setAttributes(entry, parser, attributeValues);

    QString errorMessage;
    if (!database->save(Database::Atomic, {}, &errorMessage)) {
        err << QObject::tr("Writing the database failed %1.").arg(errorMessage) << Qt::endl;
        return EXIT_FAILURE;
    }

    if (!parser->isSet(Command::QuietOption)) {
        out << QObject::tr("Successfully added entry %1.").arg(entry->title()) << Qt::endl;
    }
    return EXIT_SUCCESS;
}

namespace
{
    // Names that the CLI cannot set as custom attributes: the default attributes
    // and the entry fields have dedicated options or are resolved before custom
    // attributes by show and clip, and passkey attributes are managed internally.
    bool isReservedAttribute(const QString& name)
    {
        const QStringList reservedNames = EntryAttributes::DefaultAttributes + Utils::EntryFieldNames;
        for (const QString& reservedName : reservedNames) {
            if (name.compare(reservedName, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return EntryAttributes::isPasskeyAttribute(name);
    }
} // namespace

bool Add::validateAttributeOptions(QSharedPointer<QCommandLineParser> parser,
                                   Entry* entry,
                                   const QStringList& unprotectedNames)
{
    auto& err = Utils::STDERR;

    QStringList inlineNames;
    const QStringList attributes = parser->values(Add::AttributeOption);
    for (const QString& attribute : attributes) {
        int separator = attribute.indexOf('=');
        if (separator < 0) {
            // Do not repeat the argument, it could be a secret given by mistake
            err << QObject::tr("Invalid attribute, expected the format attribute=value.") << Qt::endl;
            return false;
        }
        inlineNames.append(attribute.left(separator));
    }
    const QStringList names = inlineNames + parser->values(Add::AttributePromptOption);

    const QStringList protectedNames = parser->values(Add::ProtectOption);
    const QStringList allNames = names + protectedNames + unprotectedNames;
    for (const QString& name : allNames) {
        if (name.isEmpty()) {
            err << QObject::tr("Attribute name cannot be empty.") << Qt::endl;
            return false;
        }
        if (isReservedAttribute(name)) {
            err << QObject::tr("Cannot use reserved attribute %1 as a custom attribute.").arg(name) << Qt::endl;
            return false;
        }
    }

    for (const QString& name : names) {
        if (names.count(name) > 1) {
            err << QObject::tr("Attribute %1 specified more than once.").arg(name) << Qt::endl;
            return false;
        }
    }

    for (const QString& name : protectedNames + unprotectedNames) {
        if (!names.contains(name) && !(entry && entry->attributes()->hasKey(name))) {
            err << QObject::tr("Attribute \"%1\" not found.").arg(name) << Qt::endl;
            return false;
        }
        if (protectedNames.contains(name) && unprotectedNames.contains(name)) {
            err << QObject::tr("Cannot both protect and unprotect attribute %1.").arg(name) << Qt::endl;
            return false;
        }
    }

    for (const QString& name : protectedNames) {
        if (inlineNames.contains(name)) {
            err << QObject::tr("WARNING: The value of attribute %1 was given on the command line, where it can be "
                               "visible to other processes and kept in the shell history. Use -A to enter it at a "
                               "prompt instead.")
                       .arg(name)
                << Qt::endl;
        }
    }

    return true;
}

bool Add::readAttributeValues(QSharedPointer<QCommandLineParser> parser, QList<QPair<QString, QString>>& values)
{
    bool quiet = parser->isSet(Command::QuietOption);
    auto& out = quiet ? Utils::DEVNULL : Utils::STDOUT;
    auto& err = Utils::STDERR;

    const QStringList inlineAttributes = parser->values(Add::AttributeOption);
    for (const QString& attribute : inlineAttributes) {
        int separator = attribute.indexOf('=');
        values.append({attribute.left(separator), attribute.mid(separator + 1)});
    }

    const QStringList promptAttributes = parser->values(Add::AttributePromptOption);
    for (const QString& name : promptAttributes) {
        out << QObject::tr("Enter value for attribute %1: ").arg(name) << Qt::flush;
        QString value = Utils::getPassword(quiet);
        // A null value means that the input ended, do not store it as an empty value
        if (value.isNull()) {
            err << QObject::tr("No value provided for attribute %1.").arg(name) << Qt::endl;
            return false;
        }
        values.append({name, value});
    }

    return true;
}

void Add::setAttributes(Entry* entry,
                        QSharedPointer<QCommandLineParser> parser,
                        const QList<QPair<QString, QString>>& values,
                        const QStringList& unprotectedNames)
{
    auto* attributes = entry->attributes();

    // Keep the current protection when only the value changes
    for (const auto& value : values) {
        attributes->set(value.first, value.second, attributes->isProtected(value.first));
    }

    const QStringList protectedNames = parser->values(Add::ProtectOption);
    for (const QString& name : protectedNames) {
        attributes->set(name, attributes->value(name), true);
    }

    for (const QString& name : unprotectedNames) {
        attributes->set(name, attributes->value(name), false);
    }
}
