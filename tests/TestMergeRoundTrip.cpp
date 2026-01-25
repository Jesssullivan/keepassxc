/*
 *  Copyright (C) 2025 KeePassXC Team <team@keepassxc.org>
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

/**
 * @file TestMergeRoundTrip.cpp
 * @brief Hermetic tests for database merge round-trip scenarios
 *
 * These tests model a bug report (#12631) where bidirectional merge between
 * two diverged databases caused data loss. The scenario:
 *   1. Two identical databases diverge with unique entries added to each
 *   2. User merges DB2 into DB1 (DB1 should have entries from both)
 *   3. User merges the now-merged DB1 back into DB2
 *   4. Expected: Both DBs contain ALL entries
 *   5. Actual (bug): BOTH databases lost all new entries from divergence period
 *
 * The tests also consider filesystem-specific issues (FAT/exFAT timestamp
 * precision, case sensitivity) that may have contributed to the bug.
 */

#include "TestMergeRoundTrip.h"
#include "mock/MockClock.h"

#include "core/Merger.h"
#include "core/Metadata.h"
#include "crypto/Crypto.h"

#include <QSignalSpy>
#include <QTest>

QTEST_GUILESS_MAIN(TestMergeRoundTrip)

namespace
{
    MockClock* m_clock = nullptr;
} // namespace

void TestMergeRoundTrip::initTestCase()
{
    qRegisterMetaType<Entry*>("Entry*");
    qRegisterMetaType<Group*>("Group*");
    QVERIFY(Crypto::init());
}

void TestMergeRoundTrip::init()
{
    Q_ASSERT(m_clock == nullptr);
    m_clock = new MockClock(2024, 1, 15, 10, 30, 0);
    MockClock::setup(m_clock);
}

void TestMergeRoundTrip::cleanup()
{
    MockClock::teardown();
    m_clock = nullptr;
}

/**
 * Helper: Create a base database with a root group and one child group
 */
Database* TestMergeRoundTrip::createBaseDatabase()
{
    auto db = new Database();

    auto mainGroup = new Group();
    mainGroup->setName("Passwords");
    mainGroup->setUuid(QUuid::createUuid());
    mainGroup->setParent(db->rootGroup());

    return db;
}

/**
 * Helper: Create a clone of a database for simulating a copy on different filesystem
 */
Database* TestMergeRoundTrip::cloneDatabase(Database* source)
{
    auto db = new Database();
    auto oldGroup = db->setRootGroup(source->rootGroup()->clone(Entry::CloneIncludeHistory,
                                                                 Group::CloneIncludeEntries));
    delete oldGroup;
    return db;
}

/**
 * Helper: Add an entry with specific title and password to a database
 */
Entry* TestMergeRoundTrip::addEntry(Database* db, const QString& title, const QString& password, Group* group)
{
    if (!group) {
        group = db->rootGroup()->children().isEmpty()
                    ? db->rootGroup()
                    : db->rootGroup()->children().first();
    }

    auto entry = new Entry();
    entry->setUuid(QUuid::createUuid());
    entry->beginUpdate();
    entry->setGroup(group);
    entry->setTitle(title);
    entry->setPassword(password);
    entry->endUpdate();

    return entry;
}

/**
 * Helper: Find an entry by title in a database (recursive search)
 */
Entry* TestMergeRoundTrip::findEntryByTitle(Database* db, const QString& title)
{
    for (Entry* entry : db->rootGroup()->entriesRecursive()) {
        if (entry->title() == title) {
            return entry;
        }
    }
    return nullptr;
}

/**
 * Helper: Count all entries in database
 */
int TestMergeRoundTrip::countEntries(Database* db)
{
    return db->rootGroup()->entriesRecursive().size();
}

// ============================================================================
// TEST 1: Basic Round-Trip Merge Test
// ============================================================================
/**
 * @brief Test basic round-trip merge with unique entries in each database
 *
 * This directly models the reported bug scenario:
 * 1. Create two identical databases
 * 2. Add unique entry "yubikey" to DB1
 * 3. Add unique entry "entry5" to DB2
 * 4. Merge DB2 into DB1 -> DB1 should have both entries
 * 5. Merge DB1 back into DB2 -> DB2 should have both entries
 * 6. Verify BOTH databases still contain ALL entries
 */
void TestMergeRoundTrip::testBasicRoundTripMerge()
{
    // Create base database and clone it (simulating two copies of same DB)
    QScopedPointer<Database> dbOriginal(createBaseDatabase());

    // Add a common entry that exists in both before divergence
    addEntry(dbOriginal.data(), "common-entry", "shared-password");

    m_clock->advanceSecond(1);

    // Clone to simulate copying the database
    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    // Verify initial state
    QCOMPARE(countEntries(db1.data()), 1);
    QCOMPARE(countEntries(db2.data()), 1);

    // ---- DIVERGENCE PERIOD ----
    // Simulate user adding different entries to each database over a day

    m_clock->advanceHour(2);  // Some time passes

    // Add entry to DB1 (the "yubi" entry from bug report)
    Entry* yubiEntry = addEntry(db1.data(), "yubikey-account", "yubi-secret-123");
    QUuid yubiUuid = yubiEntry->uuid();

    m_clock->advanceHour(1);

    // Add entry to DB2 (the "5" entry from bug report)
    Entry* entry5 = addEntry(db2.data(), "entry-five", "password5");
    QUuid entry5Uuid = entry5->uuid();

    // Verify diverged state
    QCOMPARE(countEntries(db1.data()), 2);  // common + yubi
    QCOMPARE(countEntries(db2.data()), 2);  // common + five
    QVERIFY(findEntryByTitle(db1.data(), "yubikey-account") != nullptr);
    QVERIFY(findEntryByTitle(db1.data(), "entry-five") == nullptr);
    QVERIFY(findEntryByTitle(db2.data(), "entry-five") != nullptr);
    QVERIFY(findEntryByTitle(db2.data(), "yubikey-account") == nullptr);

    m_clock->advanceSecond(1);

    // ---- FIRST MERGE: DB2 -> DB1 ----
    // User merges from DB2 into DB1
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // After first merge: DB1 should have ALL entries (common + yubi + five)
    QCOMPARE(countEntries(db1.data()), 3);
    QVERIFY(findEntryByTitle(db1.data(), "common-entry") != nullptr);
    QVERIFY(findEntryByTitle(db1.data(), "yubikey-account") != nullptr);
    QVERIFY(findEntryByTitle(db1.data(), "entry-five") != nullptr);

    m_clock->advanceSecond(1);

    // ---- SECOND MERGE: DB1 -> DB2 (round-trip) ----
    // User merges the now-complete DB1 back into DB2
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // After round-trip: DB2 should also have ALL entries
    QCOMPARE(countEntries(db2.data()), 3);
    QVERIFY(findEntryByTitle(db2.data(), "common-entry") != nullptr);
    QVERIFY(findEntryByTitle(db2.data(), "yubikey-account") != nullptr);
    QVERIFY(findEntryByTitle(db2.data(), "entry-five") != nullptr);

    // Verify UUIDs are preserved (same entries, not duplicates)
    QCOMPARE(db1.data()->rootGroup()->findEntryByUuid(yubiUuid)->title(), QString("yubikey-account"));
    QCOMPARE(db2.data()->rootGroup()->findEntryByUuid(yubiUuid)->title(), QString("yubikey-account"));
    QCOMPARE(db1.data()->rootGroup()->findEntryByUuid(entry5Uuid)->title(), QString("entry-five"));
    QCOMPARE(db2.data()->rootGroup()->findEntryByUuid(entry5Uuid)->title(), QString("entry-five"));

    // CRITICAL: Verify that the original DB1 still has all entries after the second merge
    // This tests for potential side effects of merge on the source database
    QCOMPARE(countEntries(db1.data()), 3);
}

// ============================================================================
// TEST 2: Round-Trip with Synchronize Mode (handles deletions)
// ============================================================================
/**
 * @brief Test round-trip merge with Synchronize merge mode
 *
 * The Synchronize mode propagates deletions, which could be a source of data loss
 * if the deleted objects list gets populated incorrectly during merge.
 */
void TestMergeRoundTrip::testRoundTripWithSynchronizeMode()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());
    addEntry(dbOriginal.data(), "original-entry", "original-pass");

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    // Set Synchronize mode (propagates deletions)
    db1->rootGroup()->setMergeMode(Group::Synchronize);
    db2->rootGroup()->setMergeMode(Group::Synchronize);

    m_clock->advanceHour(1);

    // Add unique entries during divergence
    Entry* entryA = addEntry(db1.data(), "entry-A", "password-A");
    QUuid uuidA = entryA->uuid();

    m_clock->advanceMinute(30);

    Entry* entryB = addEntry(db2.data(), "entry-B", "password-B");
    QUuid uuidB = entryB->uuid();

    QCOMPARE(countEntries(db1.data()), 2);
    QCOMPARE(countEntries(db2.data()), 2);

    m_clock->advanceSecond(1);

    // First merge: DB2 -> DB1
    Merger merger1(db2.data(), db1.data());
    auto changes1 = merger1.merge();

    QCOMPARE(countEntries(db1.data()), 3);  // original + A + B

    // Verify no entries were marked as deleted
    QVERIFY(!db1->containsDeletedObject(uuidA));
    QVERIFY(!db1->containsDeletedObject(uuidB));

    m_clock->advanceSecond(1);

    // Second merge: DB1 -> DB2 (round-trip)
    Merger merger2(db1.data(), db2.data());
    auto changes2 = merger2.merge();

    QCOMPARE(countEntries(db2.data()), 3);  // original + A + B

    // Verify no entries were marked as deleted in either database
    QVERIFY(!db1->containsDeletedObject(uuidA));
    QVERIFY(!db1->containsDeletedObject(uuidB));
    QVERIFY(!db2->containsDeletedObject(uuidA));
    QVERIFY(!db2->containsDeletedObject(uuidB));

    // Verify entries exist in both databases
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidA) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidB) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidA) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidB) != nullptr);
}

// ============================================================================
// TEST 3: Timestamp Precision Issues (FAT filesystem simulation)
// ============================================================================
/**
 * @brief Test merge behavior with FAT-like timestamp precision (2-second granularity)
 *
 * FAT filesystems have 2-second timestamp precision. This could cause issues
 * if entries are created within the same 2-second window but the merge logic
 * relies on sub-second timestamp differences.
 */
void TestMergeRoundTrip::testFATTimestampPrecision()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());
    addEntry(dbOriginal.data(), "base-entry", "base-pass");

    // Clone databases
    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    // Simulate FAT timestamp precision: entries created within same 2-second window
    // but in different databases (would have same truncated timestamp on FAT)

    // Add entry to DB1
    Entry* entryFAT1 = addEntry(db1.data(), "fat-entry-1", "fat-pass-1");
    QUuid uuid1 = entryFAT1->uuid();

    // Only advance by 1 second (within FAT's 2-second precision window)
    m_clock->advanceSecond(1);

    // Add entry to DB2
    Entry* entryFAT2 = addEntry(db2.data(), "fat-entry-2", "fat-pass-2");
    QUuid uuid2 = entryFAT2->uuid();

    // Both entries have timestamps within 2 seconds of each other
    QCOMPARE(countEntries(db1.data()), 2);
    QCOMPARE(countEntries(db2.data()), 2);

    m_clock->advanceSecond(2);  // Move past the precision window

    // Merge should still work correctly
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), 3);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid2) != nullptr);

    m_clock->advanceSecond(2);

    // Round-trip merge
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), 3);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr);
}

// ============================================================================
// TEST 4: Entries Created at Nearly the Same Time
// ============================================================================
/**
 * @brief Test merge with entries created at exactly the same timestamp
 *
 * Edge case where entries in different databases have identical creation times
 * (possible if databases were cloned and entries added simultaneously).
 */
void TestMergeRoundTrip::testEntriesAtSameTimestamp()
{
    QScopedPointer<Database> db1(createBaseDatabase());
    QScopedPointer<Database> db2(createBaseDatabase());

    // Don't clone - create independent databases with same structure
    // This simulates two users creating entries at exactly the same moment

    // Create entries with same timestamp (no clock advance between them)
    Entry* entry1 = addEntry(db1.data(), "simultaneous-entry-1", "pass1");
    Entry* entry2 = addEntry(db2.data(), "simultaneous-entry-2", "pass2");

    QUuid uuid1 = entry1->uuid();
    QUuid uuid2 = entry2->uuid();

    // Entries have same creation timestamp
    QDateTime time1 = entry1->timeInfo().creationTime();
    QDateTime time2 = entry2->timeInfo().creationTime();
    QCOMPARE(time1, time2);

    m_clock->advanceSecond(1);

    // Merge should handle this gracefully
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), 2);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid2) != nullptr);

    m_clock->advanceSecond(1);

    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), 2);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr);
}

// ============================================================================
// TEST 5: Special Characters in Entry Titles
// ============================================================================
/**
 * @brief Test merge with entries containing special characters like "yubi" and "5"
 *
 * The bug report mentioned entries with "yubi" and "5" in titles. Test that
 * special characters or specific strings don't cause issues.
 */
void TestMergeRoundTrip::testSpecialCharacterEntries()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    m_clock->advanceHour(1);

    // Add entries with various special characters and strings from bug report
    QList<QPair<QString, QString>> testEntries1 = {
        {"yubi-key", "yubi-pass"},
        {"YubiKey 5", "secret123"},
        {"Entry with spaces", "password"},
        {"entry@special#chars!", "p@ss!"},
        {"unicode-test", QString::fromUtf8("password")}
    };

    QList<QPair<QString, QString>> testEntries2 = {
        {"entry-5", "five-pass"},
        {"5th-entry", "fifth-secret"},
        {"Entry #5 (backup)", "backup5"},
        {"numbers-123", "num123pass"},
        {"dots.and.dashes-", "dotdash"}
    };

    QList<QUuid> uuids1;
    for (const auto& entry : testEntries1) {
        Entry* e = addEntry(db1.data(), entry.first, entry.second);
        uuids1.append(e->uuid());
        m_clock->advanceSecond(1);
    }

    QList<QUuid> uuids2;
    for (const auto& entry : testEntries2) {
        Entry* e = addEntry(db2.data(), entry.first, entry.second);
        uuids2.append(e->uuid());
        m_clock->advanceSecond(1);
    }

    int expectedTotal = testEntries1.size() + testEntries2.size();
    QCOMPARE(countEntries(db1.data()), testEntries1.size());
    QCOMPARE(countEntries(db2.data()), testEntries2.size());

    m_clock->advanceSecond(1);

    // First merge
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), expectedTotal);

    // Verify all entries from both lists exist in DB1
    for (const QUuid& uuid : uuids1) {
        QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("Missing entry from DB1: %1").arg(uuid.toString())));
    }
    for (const QUuid& uuid : uuids2) {
        QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("Missing entry from DB2: %1").arg(uuid.toString())));
    }

    m_clock->advanceSecond(1);

    // Round-trip merge
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), expectedTotal);

    // Verify all entries exist in DB2 after round-trip
    for (const QUuid& uuid : uuids1) {
        QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("Missing entry after round-trip: %1").arg(uuid.toString())));
    }
    for (const QUuid& uuid : uuids2) {
        QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("Missing entry after round-trip: %1").arg(uuid.toString())));
    }
}

// ============================================================================
// TEST 6: Multiple Sequential Round-Trip Merges
// ============================================================================
/**
 * @brief Test multiple consecutive round-trip merges don't cause data loss
 *
 * Repeated merge cycles could accumulate issues in deleted objects list
 * or cause other problems.
 */
void TestMergeRoundTrip::testMultipleRoundTrips()
{
    QScopedPointer<Database> db1(createBaseDatabase());
    QScopedPointer<Database> db2(cloneDatabase(db1.data()));

    db1->rootGroup()->setMergeMode(Group::Synchronize);
    db2->rootGroup()->setMergeMode(Group::Synchronize);

    QList<QUuid> allEntryUuids;

    // Perform multiple round-trip cycles, adding entries each time
    for (int cycle = 0; cycle < 5; ++cycle) {
        m_clock->advanceHour(1);

        // Add entry to DB1
        Entry* e1 = addEntry(db1.data(), QString("cycle%1-db1-entry").arg(cycle),
                             QString("pass-cycle%1-1").arg(cycle));
        allEntryUuids.append(e1->uuid());

        m_clock->advanceMinute(10);

        // Add entry to DB2
        Entry* e2 = addEntry(db2.data(), QString("cycle%1-db2-entry").arg(cycle),
                             QString("pass-cycle%1-2").arg(cycle));
        allEntryUuids.append(e2->uuid());

        m_clock->advanceSecond(1);

        // Merge DB2 -> DB1
        Merger merger1(db2.data(), db1.data());
        merger1.merge();

        m_clock->advanceSecond(1);

        // Merge DB1 -> DB2 (round-trip)
        Merger merger2(db1.data(), db2.data());
        merger2.merge();

        // Verify both databases have all entries after each cycle
        int expectedCount = (cycle + 1) * 2;
        QCOMPARE(countEntries(db1.data()), expectedCount);
        QCOMPARE(countEntries(db2.data()), expectedCount);

        // Verify no entries were marked as deleted
        for (const QUuid& uuid : allEntryUuids) {
            QVERIFY2(!db1->containsDeletedObject(uuid),
                     qPrintable(QString("Entry incorrectly deleted in DB1: %1").arg(uuid.toString())));
            QVERIFY2(!db2->containsDeletedObject(uuid),
                     qPrintable(QString("Entry incorrectly deleted in DB2: %1").arg(uuid.toString())));
            QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid) != nullptr,
                     qPrintable(QString("Entry missing from DB1: %1").arg(uuid.toString())));
            QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid) != nullptr,
                     qPrintable(QString("Entry missing from DB2: %1").arg(uuid.toString())));
        }
    }
}

// ============================================================================
// TEST 7: Merge After One Database Had Entries Deleted
// ============================================================================
/**
 * @brief Test that legitimate deletions are handled correctly in round-trip
 *
 * Ensure that actual deletions are properly tracked and don't cause
 * unintended deletion of other entries.
 */
void TestMergeRoundTrip::testRoundTripWithDeletions()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());
    Entry* toKeep = addEntry(dbOriginal.data(), "entry-to-keep", "keep-pass");
    QUuid keepUuid = toKeep->uuid();

    m_clock->advanceSecond(1);

    Entry* toDelete = addEntry(dbOriginal.data(), "entry-to-delete", "delete-pass");
    QUuid deleteUuid = toDelete->uuid();

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    db1->rootGroup()->setMergeMode(Group::Synchronize);
    db2->rootGroup()->setMergeMode(Group::Synchronize);

    m_clock->advanceHour(1);

    // Add new entries during divergence
    Entry* newEntry1 = addEntry(db1.data(), "new-in-db1", "new-pass-1");
    QUuid new1Uuid = newEntry1->uuid();

    m_clock->advanceMinute(10);

    Entry* newEntry2 = addEntry(db2.data(), "new-in-db2", "new-pass-2");
    QUuid new2Uuid = newEntry2->uuid();

    m_clock->advanceMinute(10);

    // Delete an entry from DB1 (legitimate deletion)
    Entry* entryToDeleteInDb1 = db1->rootGroup()->findEntryByUuid(deleteUuid);
    QVERIFY(entryToDeleteInDb1 != nullptr);
    delete entryToDeleteInDb1;
    QVERIFY(db1->containsDeletedObject(deleteUuid));

    m_clock->advanceSecond(1);

    // First merge: DB2 -> DB1
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // DB1 should have: entry-to-keep, new-in-db1, new-in-db2
    // entry-to-delete was deleted in DB1, but exists in DB2
    // With Synchronize mode, since the entry exists in source (DB2) and was deleted
    // in target after source's knowledge, behavior depends on timestamps

    // After round-trip, the new entries should NOT be affected by the deletion
    QVERIFY(db1->rootGroup()->findEntryByUuid(keepUuid) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(new1Uuid) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(new2Uuid) != nullptr);

    m_clock->advanceSecond(1);

    // Round-trip merge: DB1 -> DB2
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // The new entries should still exist in DB2
    QVERIFY2(db2->rootGroup()->findEntryByUuid(keepUuid) != nullptr,
             "entry-to-keep was incorrectly deleted");
    QVERIFY2(db2->rootGroup()->findEntryByUuid(new1Uuid) != nullptr,
             "new-in-db1 was incorrectly deleted");
    QVERIFY2(db2->rootGroup()->findEntryByUuid(new2Uuid) != nullptr,
             "new-in-db2 was incorrectly deleted");
}

// ============================================================================
// TEST 8: Large Divergence Window (Days of Independent Changes)
// ============================================================================
/**
 * @brief Test merge behavior after extended divergence period
 *
 * Simulates the bug report scenario of databases diverging "over a day or two"
 */
void TestMergeRoundTrip::testLargeDivergenceWindow()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    QList<QUuid> db1Entries;
    QList<QUuid> db2Entries;

    // Simulate Day 1 - entries added to DB1
    m_clock->advanceDay(1);
    for (int i = 0; i < 3; ++i) {
        Entry* e = addEntry(db1.data(), QString("day1-db1-entry%1").arg(i), QString("pass%1").arg(i));
        db1Entries.append(e->uuid());
        m_clock->advanceHour(2);
    }

    // Simulate Day 1 - entries added to DB2
    m_clock->advanceHour(-6);  // Reset to earlier in day 1
    for (int i = 0; i < 3; ++i) {
        Entry* e = addEntry(db2.data(), QString("day1-db2-entry%1").arg(i), QString("pass%1").arg(i));
        db2Entries.append(e->uuid());
        m_clock->advanceHour(2);
    }

    // Simulate Day 2 - more entries
    m_clock->advanceDay(1);
    for (int i = 3; i < 5; ++i) {
        Entry* e = addEntry(db1.data(), QString("day2-db1-entry%1").arg(i), QString("pass%1").arg(i));
        db1Entries.append(e->uuid());
        m_clock->advanceHour(3);
    }

    m_clock->advanceHour(-6);
    for (int i = 3; i < 5; ++i) {
        Entry* e = addEntry(db2.data(), QString("day2-db2-entry%1").arg(i), QString("pass%1").arg(i));
        db2Entries.append(e->uuid());
        m_clock->advanceHour(3);
    }

    m_clock->advanceDay(1);

    int expectedTotal = db1Entries.size() + db2Entries.size();
    QCOMPARE(countEntries(db1.data()), db1Entries.size());
    QCOMPARE(countEntries(db2.data()), db2Entries.size());

    // First merge
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), expectedTotal);

    m_clock->advanceSecond(1);

    // Round-trip merge
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), expectedTotal);

    // Verify all entries exist
    for (const QUuid& uuid : db1Entries) {
        QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("DB1 entry missing from DB1: %1").arg(uuid.toString())));
        QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("DB1 entry missing from DB2: %1").arg(uuid.toString())));
    }
    for (const QUuid& uuid : db2Entries) {
        QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("DB2 entry missing from DB1: %1").arg(uuid.toString())));
        QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid) != nullptr,
                 qPrintable(QString("DB2 entry missing from DB2: %1").arg(uuid.toString())));
    }
}

// ============================================================================
// TEST 9: Merge with KeepNewer Mode
// ============================================================================
/**
 * @brief Test round-trip merge with KeepNewer merge mode
 *
 * KeepNewer mode should not propagate deletions but should merge entry changes
 */
void TestMergeRoundTrip::testRoundTripWithKeepNewerMode()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());
    addEntry(dbOriginal.data(), "original", "original-pass");

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    // Set KeepNewer mode (does not propagate deletions)
    db1->rootGroup()->setMergeMode(Group::KeepNewer);
    db2->rootGroup()->setMergeMode(Group::KeepNewer);

    m_clock->advanceHour(1);

    Entry* entryA = addEntry(db1.data(), "keepnewer-A", "pass-A");
    QUuid uuidA = entryA->uuid();

    m_clock->advanceMinute(30);

    Entry* entryB = addEntry(db2.data(), "keepnewer-B", "pass-B");
    QUuid uuidB = entryB->uuid();

    m_clock->advanceSecond(1);

    // First merge
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), 3);  // original + A + B

    m_clock->advanceSecond(1);

    // Round-trip
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), 3);  // original + A + B

    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidA) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidB) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidA) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidB) != nullptr);
}

// ============================================================================
// TEST 10: Entry Modifications During Divergence
// ============================================================================
/**
 * @brief Test that entry modifications are preserved during round-trip merge
 *
 * Not just new entries, but modifications to existing entries should be
 * correctly merged.
 */
void TestMergeRoundTrip::testEntryModificationsDuringDivergence()
{
    QScopedPointer<Database> dbOriginal(createBaseDatabase());
    Entry* sharedEntry = addEntry(dbOriginal.data(), "shared-entry", "original-password");
    QUuid sharedUuid = sharedEntry->uuid();

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db1(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> db2(cloneDatabase(dbOriginal.data()));

    m_clock->advanceHour(1);

    // Add new entries to both
    Entry* newEntry1 = addEntry(db1.data(), "new-in-db1", "pass1");
    QUuid uuid1 = newEntry1->uuid();

    m_clock->advanceMinute(10);

    Entry* newEntry2 = addEntry(db2.data(), "new-in-db2", "pass2");
    QUuid uuid2 = newEntry2->uuid();

    m_clock->advanceMinute(10);

    // Modify the shared entry in DB1
    Entry* sharedInDb1 = db1->rootGroup()->findEntryByUuid(sharedUuid);
    sharedInDb1->beginUpdate();
    sharedInDb1->setPassword("modified-in-db1");
    sharedInDb1->setNotes("Modified by DB1");
    sharedInDb1->endUpdate();

    m_clock->advanceSecond(1);

    // First merge: DB2 -> DB1
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // DB1 should have all entries and keep its modification (it's newer)
    QCOMPARE(countEntries(db1.data()), 3);
    Entry* mergedShared1 = db1->rootGroup()->findEntryByUuid(sharedUuid);
    QCOMPARE(mergedShared1->password(), QString("modified-in-db1"));

    m_clock->advanceSecond(1);

    // Round-trip: DB1 -> DB2
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // DB2 should have all entries and receive the modification
    QCOMPARE(countEntries(db2.data()), 3);
    Entry* mergedShared2 = db2->rootGroup()->findEntryByUuid(sharedUuid);
    QCOMPARE(mergedShared2->password(), QString("modified-in-db1"));

    // Verify new entries exist in both
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid2) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr);
}

// ============================================================================
// TEST 11: Empty Databases Merge
// ============================================================================
/**
 * @brief Test merge behavior with initially empty databases
 */
void TestMergeRoundTrip::testEmptyDatabaseMerge()
{
    QScopedPointer<Database> db1(new Database());
    QScopedPointer<Database> db2(new Database());

    m_clock->advanceSecond(1);

    Entry* entry1 = addEntry(db1.data(), "first-entry", "pass1");
    QUuid uuid1 = entry1->uuid();

    m_clock->advanceSecond(1);

    Entry* entry2 = addEntry(db2.data(), "second-entry", "pass2");
    QUuid uuid2 = entry2->uuid();

    m_clock->advanceSecond(1);

    // Merge empty DB2 into DB1 that has entries
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    QCOMPARE(countEntries(db1.data()), 2);

    m_clock->advanceSecond(1);

    // Round-trip
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    QCOMPARE(countEntries(db2.data()), 2);

    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuid2) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr);
}

// ============================================================================
// TEST 12: Nested Groups in Diverged Databases
// ============================================================================
/**
 * @brief Test round-trip merge with entries in nested group structures
 *
 * Entries in different nested groups should all be preserved.
 */
void TestMergeRoundTrip::testNestedGroupsMerge()
{
    QScopedPointer<Database> db1(createBaseDatabase());

    // Create nested structure in DB1
    Group* level1 = new Group();
    level1->setName("Level1");
    level1->setUuid(QUuid::createUuid());
    level1->setParent(db1->rootGroup());

    Group* level2 = new Group();
    level2->setName("Level2");
    level2->setUuid(QUuid::createUuid());
    level2->setParent(level1);

    m_clock->advanceSecond(1);

    QScopedPointer<Database> db2(cloneDatabase(db1.data()));

    m_clock->advanceHour(1);

    // Add entries at different nesting levels in each DB
    Entry* rootEntry1 = addEntry(db1.data(), "root-entry-db1", "pass", db1->rootGroup());
    Entry* level1Entry1 = addEntry(db1.data(), "level1-entry-db1", "pass",
                                   db1->rootGroup()->findChildByName("Level1"));
    Entry* level2Entry1 = addEntry(db1.data(), "level2-entry-db1", "pass",
                                   db1->rootGroup()->findChildByName("Level1")->findChildByName("Level2"));

    QUuid uuidRoot1 = rootEntry1->uuid();
    QUuid uuidL1_1 = level1Entry1->uuid();
    QUuid uuidL2_1 = level2Entry1->uuid();

    m_clock->advanceMinute(30);

    Entry* rootEntry2 = addEntry(db2.data(), "root-entry-db2", "pass", db2->rootGroup());
    Entry* level1Entry2 = addEntry(db2.data(), "level1-entry-db2", "pass",
                                   db2->rootGroup()->findChildByName("Level1"));
    Entry* level2Entry2 = addEntry(db2.data(), "level2-entry-db2", "pass",
                                   db2->rootGroup()->findChildByName("Level1")->findChildByName("Level2"));

    QUuid uuidRoot2 = rootEntry2->uuid();
    QUuid uuidL1_2 = level1Entry2->uuid();
    QUuid uuidL2_2 = level2Entry2->uuid();

    m_clock->advanceSecond(1);

    // First merge
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // Verify all entries from both DBs are in DB1
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidRoot1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidL1_1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidL2_1) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidRoot2) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidL1_2) != nullptr);
    QVERIFY(db1->rootGroup()->findEntryByUuid(uuidL2_2) != nullptr);

    m_clock->advanceSecond(1);

    // Round-trip
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // Verify all entries exist in DB2
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidRoot1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidL1_1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidL2_1) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidRoot2) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidL1_2) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuidL2_2) != nullptr);
}

// ============================================================================
// TEST 13: Verify Source Database Not Modified By Merge
// ============================================================================
/**
 * @brief Ensure that the source database in a merge is not modified
 *
 * The merge operation should only modify the target database.
 */
void TestMergeRoundTrip::testSourceDatabaseNotModified()
{
    QScopedPointer<Database> db1(createBaseDatabase());
    QScopedPointer<Database> db2(createBaseDatabase());

    m_clock->advanceSecond(1);

    Entry* entry1 = addEntry(db1.data(), "db1-entry", "pass1");
    QUuid uuid1 = entry1->uuid();

    m_clock->advanceSecond(1);

    Entry* entry2 = addEntry(db2.data(), "db2-entry", "pass2");
    QUuid uuid2 = entry2->uuid();

    // Record initial state of db2
    int db2InitialCount = countEntries(db2.data());
    int db2InitialDeletedCount = db2->deletedObjects().size();

    m_clock->advanceSecond(1);

    // Merge db2 (source) into db1 (target)
    Merger merger(db2.data(), db1.data());
    merger.merge();

    // Verify db1 (target) was modified
    QCOMPARE(countEntries(db1.data()), 2);

    // Verify db2 (source) was NOT modified
    QCOMPARE(countEntries(db2.data()), db2InitialCount);
    QCOMPARE(db2->deletedObjects().size(), db2InitialDeletedCount);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr);
    QVERIFY(db2->rootGroup()->findEntryByUuid(uuid1) == nullptr);  // Should NOT have db1's entry
}

// ============================================================================
// TEST 14: Deletion Marker Timestamp Equality (Regression Test)
// ============================================================================
/**
 * @brief Regression test: Entry survives when lastModificationTime == deletionTime
 *
 * This tests the fix from `>` to `>=` in the timestamp comparison.
 * With the buggy code (using `>`), an entry with lastModificationTime exactly equal
 * to deletionTime would be deleted. With the fix (`>=`), it survives.
 *
 * This is critical for FAT filesystems where timestamp precision is 2 seconds,
 * making timestamp equality much more likely.
 */
void TestMergeRoundTrip::testDeletionMarkerTimestampEquality()
{
    // Create target database with an entry
    QScopedPointer<Database> dbTarget(createBaseDatabase());
    dbTarget->rootGroup()->setMergeMode(Group::Synchronize);

    Entry* entry = addEntry(dbTarget.data(), "timestamp-test-entry", "test-pass");
    QUuid entryUuid = entry->uuid();
    QDateTime entryModTime = entry->timeInfo().lastModificationTime();

    // Create source database with a deletion marker at EXACTLY the same timestamp
    QScopedPointer<Database> dbSource(new Database());
    dbSource->rootGroup()->setMergeMode(Group::Synchronize);

    // Add deletion marker with deletionTime == entry's lastModificationTime
    DeletedObject delObj;
    delObj.uuid = entryUuid;
    delObj.deletionTime = entryModTime;  // EXACTLY equal
    dbSource->addDeletedObject(delObj);

    // Verify setup
    QVERIFY(dbTarget->rootGroup()->findEntryByUuid(entryUuid) != nullptr);
    QVERIFY(dbSource->containsDeletedObject(entryUuid));
    QCOMPARE(dbSource->deletedObjects().first().deletionTime, entryModTime);

    // Merge source (with deletion marker) into target (with entry)
    Merger merger(dbSource.data(), dbTarget.data());
    merger.merge();

    // WITH FIX: Entry should SURVIVE (lastModificationTime >= deletionTime)
    // WITHOUT FIX: Entry would be DELETED (lastModificationTime > deletionTime is false)
    Entry* survivingEntry = dbTarget->rootGroup()->findEntryByUuid(entryUuid);
    QVERIFY2(survivingEntry != nullptr,
             "REGRESSION: Entry was deleted when lastModificationTime == deletionTime. "
             "The fix should use >= instead of > for timestamp comparison.");
}

// ============================================================================
// TEST 15: Entry Re-added After Deletion (locationChanged Test)
// ============================================================================
/**
 * @brief Regression test: Entry survives based on locationChanged timestamp
 *
 * This tests the fix that considers locationChanged in addition to lastModificationTime.
 * Scenario:
 * 1. Entry originally created at T1 (lastModificationTime = T1)
 * 2. Entry deleted from DB-A at T2, creating DeletedObject(deletionTime = T2)
 * 3. Entry re-added to DB-B at T3 (locationChanged = T3, but lastModificationTime still T1)
 * 4. When merging DB-A into DB-B, entry should survive because locationChanged (T3) > deletionTime (T2)
 *
 * Without the fix, the entry would be deleted because lastModificationTime (T1) < deletionTime (T2).
 */
void TestMergeRoundTrip::testEntryReaddedAfterDeletion()
{
    // Step 1: Create entry at T1
    QScopedPointer<Database> dbTarget(createBaseDatabase());
    dbTarget->rootGroup()->setMergeMode(Group::Synchronize);

    Entry* entry = addEntry(dbTarget.data(), "readded-entry", "original-pass");
    QUuid entryUuid = entry->uuid();
    QDateTime T1 = entry->timeInfo().lastModificationTime();

    // Step 2: Advance time and create deletion marker at T2
    m_clock->advanceHour(1);
    QDateTime T2 = m_clock->currentDateTimeUtc();

    QScopedPointer<Database> dbSource(new Database());
    dbSource->rootGroup()->setMergeMode(Group::Synchronize);

    DeletedObject delObj;
    delObj.uuid = entryUuid;
    delObj.deletionTime = T2;
    dbSource->addDeletedObject(delObj);

    // Step 3: Advance time to T3 and simulate re-adding the entry (update locationChanged)
    m_clock->advanceHour(1);
    QDateTime T3 = m_clock->currentDateTimeUtc();

    // Manually update the entry's locationChanged to T3 to simulate it being re-added
    // This mimics the scenario where an entry is imported/restored after being deleted elsewhere
    Entry* targetEntry = dbTarget->rootGroup()->findEntryByUuid(entryUuid);
    QVERIFY(targetEntry != nullptr);

    TimeInfo timeInfo = targetEntry->timeInfo();
    timeInfo.setLocationChanged(T3);
    targetEntry->setTimeInfo(timeInfo);

    // Verify setup:
    // - Entry's lastModificationTime (T1) < deletionTime (T2) - would fail old check
    // - Entry's locationChanged (T3) > deletionTime (T2) - should pass new check
    QVERIFY(targetEntry->timeInfo().lastModificationTime() < T2);  // T1 < T2
    QVERIFY(targetEntry->timeInfo().locationChanged() > T2);        // T3 > T2

    // Merge source (with deletion marker at T2) into target (with entry re-added at T3)
    Merger merger(dbSource.data(), dbTarget.data());
    merger.merge();

    // WITH FIX: Entry should SURVIVE (max(lastModificationTime, locationChanged) = T3 >= T2)
    // WITHOUT FIX: Entry would be DELETED (lastModificationTime = T1 < T2)
    Entry* survivingEntry = dbTarget->rootGroup()->findEntryByUuid(entryUuid);
    QVERIFY2(survivingEntry != nullptr,
             "REGRESSION: Entry was deleted despite being re-added after deletion. "
             "The fix should consider locationChanged in addition to lastModificationTime.");
}

// ============================================================================
// TEST 16: Different Database UUIDs (Issue 2 Investigation)
// ============================================================================
/**
 * @brief Test merge between databases with different root group UUIDs
 *
 * This tests the scenario where databases are not file copies but were
 * created independently (or the UUID was regenerated). This is what the
 * user experienced when they saw different database UUIDs between their
 * FAT32 and exFAT copies.
 */
void TestMergeRoundTrip::testDifferentDatabaseUUIDs()
{
    // Create two completely independent databases (different UUIDs)
    QScopedPointer<Database> db1(new Database());
    QScopedPointer<Database> db2(new Database());

    // Verify they have different root group UUIDs
    QVERIFY(db1->rootGroup()->uuid() != db2->rootGroup()->uuid());

    m_clock->advanceSecond(1);

    // Add unique entries to each
    Entry* entry1 = addEntry(db1.data(), "db1-unique-entry", "pass1");
    QUuid uuid1 = entry1->uuid();

    m_clock->advanceSecond(1);

    Entry* entry2 = addEntry(db2.data(), "db2-unique-entry", "pass2");
    QUuid uuid2 = entry2->uuid();

    QCOMPARE(countEntries(db1.data()), 1);
    QCOMPARE(countEntries(db2.data()), 1);

    m_clock->advanceSecond(1);

    // First merge: DB2 -> DB1
    Merger merger1(db2.data(), db1.data());
    auto changes1 = merger1.merge();

    // DB1 should have both entries
    QCOMPARE(countEntries(db1.data()), 2);
    QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid1) != nullptr,
             "DB1's own entry missing after merge");
    QVERIFY2(db1->rootGroup()->findEntryByUuid(uuid2) != nullptr,
             "DB2's entry not added to DB1 during merge");

    m_clock->advanceSecond(1);

    // Round-trip: DB1 -> DB2
    Merger merger2(db1.data(), db2.data());
    auto changes2 = merger2.merge();

    // DB2 should have both entries
    QCOMPARE(countEntries(db2.data()), 2);
    QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid1) != nullptr,
             "DB1's entry not added to DB2 during round-trip");
    QVERIFY2(db2->rootGroup()->findEntryByUuid(uuid2) != nullptr,
             "DB2's own entry missing after round-trip");
}

// ============================================================================
// TEST 17: Merge With Group Structure Mismatch (Issue 2 Investigation)
// ============================================================================
/**
 * @brief Test merge when source has groups that don't exist in target
 *
 * This tests the scenario where entries in the source database are in groups
 * that don't exist in the target database. The merge should create the
 * groups and correctly place entries.
 */
void TestMergeRoundTrip::testMergeWithGroupMismatch()
{
    QScopedPointer<Database> db1(new Database());
    QScopedPointer<Database> db2(new Database());

    // Create unique group structure in DB1
    Group* groupA = new Group();
    groupA->setName("GroupA");
    groupA->setUuid(QUuid::createUuid());
    groupA->setParent(db1->rootGroup());

    m_clock->advanceSecond(1);

    // Create different group structure in DB2
    Group* groupB = new Group();
    groupB->setName("GroupB");
    groupB->setUuid(QUuid::createUuid());
    groupB->setParent(db2->rootGroup());

    m_clock->advanceSecond(1);

    // Add entry to DB1's GroupA
    Entry* entryInGroupA = addEntry(db1.data(), "entry-in-group-a", "passA", groupA);
    QUuid uuidA = entryInGroupA->uuid();

    m_clock->advanceSecond(1);

    // Add entry to DB2's GroupB
    Entry* entryInGroupB = addEntry(db2.data(), "entry-in-group-b", "passB", groupB);
    QUuid uuidB = entryInGroupB->uuid();

    // Verify initial state
    QCOMPARE(countEntries(db1.data()), 1);
    QCOMPARE(countEntries(db2.data()), 1);
    QCOMPARE(entryInGroupA->group()->name(), QString("GroupA"));
    QCOMPARE(entryInGroupB->group()->name(), QString("GroupB"));

    m_clock->advanceSecond(1);

    // Merge DB2 -> DB1 (DB1 doesn't have GroupB)
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // DB1 should have both entries
    QCOMPARE(countEntries(db1.data()), 2);
    Entry* entryA_in_db1 = db1->rootGroup()->findEntryByUuid(uuidA);
    Entry* entryB_in_db1 = db1->rootGroup()->findEntryByUuid(uuidB);

    QVERIFY2(entryA_in_db1 != nullptr, "Entry from GroupA missing after merge");
    QVERIFY2(entryB_in_db1 != nullptr, "Entry from GroupB not added during merge");

    // Verify entry B is in a group (not orphaned)
    QVERIFY2(entryB_in_db1->group() != nullptr, "ORPHANING: Entry has no parent group!");
    QCOMPARE(entryB_in_db1->group()->name(), QString("GroupB"));

    m_clock->advanceSecond(1);

    // Round-trip: DB1 -> DB2
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // DB2 should have both entries
    QCOMPARE(countEntries(db2.data()), 2);
    Entry* entryA_in_db2 = db2->rootGroup()->findEntryByUuid(uuidA);
    Entry* entryB_in_db2 = db2->rootGroup()->findEntryByUuid(uuidB);

    QVERIFY2(entryA_in_db2 != nullptr, "Entry from GroupA not added to DB2");
    QVERIFY2(entryB_in_db2 != nullptr, "Entry from GroupB missing after round-trip");

    // Verify entries are in groups (not orphaned)
    QVERIFY2(entryA_in_db2->group() != nullptr, "ORPHANING: Entry A has no parent group!");
    QVERIFY2(entryB_in_db2->group() != nullptr, "ORPHANING: Entry B has no parent group!");
}

// ============================================================================
// TEST 18: Entries Recoverable After Merge (Issue 2 Investigation)
// ============================================================================
/**
 * @brief Test that entries are findable via multiple methods after merge
 *
 * This tests the specific symptom from the bug report: entries not visible
 * in search but recoverable via CLI. We verify entries are findable via:
 * - findEntryByUuid (what CLI uses)
 * - entriesRecursive (what search uses)
 * - Direct group iteration
 */
void TestMergeRoundTrip::testEntriesRecoverableAfterMerge()
{
    QScopedPointer<Database> db1(createBaseDatabase());
    QScopedPointer<Database> db2(cloneDatabase(db1.data()));

    m_clock->advanceHour(1);

    // Add entries with recognizable titles (like "yubikey" from bug report)
    Entry* yubiEntry = addEntry(db1.data(), "yubikey-pin", "secret-pin");
    QUuid yubiUuid = yubiEntry->uuid();

    m_clock->advanceMinute(10);

    Entry* entry5 = addEntry(db2.data(), "entry-number-5", "password5");
    QUuid uuid5 = entry5->uuid();

    m_clock->advanceSecond(1);

    // First merge
    Merger merger1(db2.data(), db1.data());
    merger1.merge();

    // Verify via multiple methods that entries are findable in DB1
    // Method 1: findEntryByUuid
    Entry* foundYubi_byUuid = db1->rootGroup()->findEntryByUuid(yubiUuid);
    Entry* found5_byUuid = db1->rootGroup()->findEntryByUuid(uuid5);
    QVERIFY2(foundYubi_byUuid != nullptr, "yubikey entry not found by UUID");
    QVERIFY2(found5_byUuid != nullptr, "entry5 not found by UUID");

    // Method 2: entriesRecursive (what search uses)
    QList<Entry*> allEntries = db1->rootGroup()->entriesRecursive();
    bool foundYubi_inList = false;
    bool found5_inList = false;
    for (Entry* e : allEntries) {
        if (e->uuid() == yubiUuid) foundYubi_inList = true;
        if (e->uuid() == uuid5) found5_inList = true;
    }
    QVERIFY2(foundYubi_inList, "ORPHANING: yubikey entry not in entriesRecursive!");
    QVERIFY2(found5_inList, "ORPHANING: entry5 not in entriesRecursive!");

    // Method 3: findEntryByTitle
    Entry* foundYubi_byTitle = findEntryByTitle(db1.data(), "yubikey-pin");
    Entry* found5_byTitle = findEntryByTitle(db1.data(), "entry-number-5");
    QVERIFY2(foundYubi_byTitle != nullptr, "yubikey entry not found by title");
    QVERIFY2(found5_byTitle != nullptr, "entry5 not found by title");

    m_clock->advanceSecond(1);

    // Round-trip merge
    Merger merger2(db1.data(), db2.data());
    merger2.merge();

    // Same verification for DB2 after round-trip
    allEntries = db2->rootGroup()->entriesRecursive();
    foundYubi_inList = false;
    found5_inList = false;
    for (Entry* e : allEntries) {
        if (e->uuid() == yubiUuid) foundYubi_inList = true;
        if (e->uuid() == uuid5) found5_inList = true;
    }
    QVERIFY2(foundYubi_inList, "ORPHANING: yubikey entry not in DB2's entriesRecursive after round-trip!");
    QVERIFY2(found5_inList, "ORPHANING: entry5 not in DB2's entriesRecursive after round-trip!");
}

// ============================================================================
// TEST 19: Bidirectional Merge With KeepNewer Mode (Issue 2 Investigation)
// ============================================================================
/**
 * @brief Test bidirectional merge using KeepNewer mode (the default for GUI merge)
 *
 * This directly simulates the user's scenario: two copies of a database with
 * unique entries added to each, merged bidirectionally using the default
 * KeepNewer mode that the GUI uses.
 */
void TestMergeRoundTrip::testBidirectionalMergeWithKeepNewer()
{
    // Simulate the user's exact scenario:
    // 1. Create database on macOS
    // 2. Copy to FAT32 and exFAT (simulate with cloneDatabase)
    // 3. Add unique entries to each
    // 4. Merge bidirectionally

    QScopedPointer<Database> dbOriginal(createBaseDatabase());

    // Add some common entries before copying
    addEntry(dbOriginal.data(), "common-account-1", "commonpass1");
    addEntry(dbOriginal.data(), "common-account-2", "commonpass2");

    m_clock->advanceSecond(1);

    // Clone to simulate copying to FAT32 and exFAT
    QScopedPointer<Database> dbFAT32(cloneDatabase(dbOriginal.data()));
    QScopedPointer<Database> dbExFAT(cloneDatabase(dbOriginal.data()));

    // Explicitly set KeepNewer mode (this is the default, but being explicit)
    dbFAT32->rootGroup()->setMergeMode(Group::KeepNewer);
    dbExFAT->rootGroup()->setMergeMode(Group::KeepNewer);

    QCOMPARE(countEntries(dbFAT32.data()), 2);
    QCOMPARE(countEntries(dbExFAT.data()), 2);

    // Simulate user adding entries during "a day or two" of independent use
    m_clock->advanceDay(1);

    // User adds yubikey entries to FAT32 database
    Entry* yubiEntry = addEntry(dbFAT32.data(), "yubikey-pin-fat32", "yubi-secret");
    QUuid yubiUuid = yubiEntry->uuid();

    m_clock->advanceHour(2);

    Entry* pinEntry = addEntry(dbFAT32.data(), "pin-codes-fat32", "1234");
    QUuid pinUuid = pinEntry->uuid();

    // User adds different entries to exFAT database
    m_clock->advanceHour(1);

    Entry* entry5 = addEntry(dbExFAT.data(), "entry-5-exfat", "pass5");
    QUuid uuid5 = entry5->uuid();

    m_clock->advanceHour(2);

    Entry* backupEntry = addEntry(dbExFAT.data(), "backup-codes-exfat", "backup123");
    QUuid backupUuid = backupEntry->uuid();

    // After divergence: FAT32 has 4 entries, exFAT has 4 entries
    QCOMPARE(countEntries(dbFAT32.data()), 4);
    QCOMPARE(countEntries(dbExFAT.data()), 4);

    m_clock->advanceSecond(1);

    // User performs first merge: exFAT -> FAT32
    // (This is what the GUI does with Database > Merge From Database)
    Merger merger1(dbExFAT.data(), dbFAT32.data());
    auto changes1 = merger1.merge();

    // FAT32 should now have 6 entries (2 common + 2 FAT32 + 2 exFAT)
    QCOMPARE(countEntries(dbFAT32.data()), 6);

    // Verify all entries are findable
    QVERIFY2(dbFAT32->rootGroup()->findEntryByUuid(yubiUuid) != nullptr,
             "ISSUE 2: yubikey entry missing after first merge!");
    QVERIFY2(dbFAT32->rootGroup()->findEntryByUuid(pinUuid) != nullptr,
             "ISSUE 2: pin entry missing after first merge!");
    QVERIFY2(dbFAT32->rootGroup()->findEntryByUuid(uuid5) != nullptr,
             "ISSUE 2: entry5 not added during first merge!");
    QVERIFY2(dbFAT32->rootGroup()->findEntryByUuid(backupUuid) != nullptr,
             "ISSUE 2: backup entry not added during first merge!");

    m_clock->advanceSecond(1);

    // User performs second merge: FAT32 -> exFAT (round-trip)
    Merger merger2(dbFAT32.data(), dbExFAT.data());
    auto changes2 = merger2.merge();

    // exFAT should now have 6 entries too
    QCOMPARE(countEntries(dbExFAT.data()), 6);

    // Verify ALL entries are findable in exFAT after round-trip
    QVERIFY2(dbExFAT->rootGroup()->findEntryByUuid(yubiUuid) != nullptr,
             "ISSUE 2: yubikey entry missing after round-trip!");
    QVERIFY2(dbExFAT->rootGroup()->findEntryByUuid(pinUuid) != nullptr,
             "ISSUE 2: pin entry missing after round-trip!");
    QVERIFY2(dbExFAT->rootGroup()->findEntryByUuid(uuid5) != nullptr,
             "ISSUE 2: entry5 missing after round-trip!");
    QVERIFY2(dbExFAT->rootGroup()->findEntryByUuid(backupUuid) != nullptr,
             "ISSUE 2: backup entry missing after round-trip!");

    // Verify entries are in entriesRecursive (what GUI search uses)
    QList<Entry*> fat32Entries = dbFAT32->rootGroup()->entriesRecursive();
    QList<Entry*> exfatEntries = dbExFAT->rootGroup()->entriesRecursive();

    QCOMPARE(fat32Entries.size(), 6);
    QCOMPARE(exfatEntries.size(), 6);

    // Final verification: both databases have identical entry UUIDs
    QSet<QUuid> fat32Uuids, exfatUuids;
    for (Entry* e : fat32Entries) fat32Uuids.insert(e->uuid());
    for (Entry* e : exfatEntries) exfatUuids.insert(e->uuid());

    QCOMPARE(fat32Uuids, exfatUuids);
}
