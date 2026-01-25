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

#ifndef KEEPASSX_TESTMERGEROUNDTRIP_H
#define KEEPASSX_TESTMERGEROUNDTRIP_H

#include "core/Database.h"

/**
 * @class TestMergeRoundTrip
 * @brief Hermetic tests for bidirectional database merge scenarios
 *
 * These tests specifically target a bug (#12631) where round-trip merging
 * between two diverged databases caused data loss. The tests cover:
 *
 * - Basic round-trip merge preservation of all entries
 * - Synchronize vs KeepNewer merge mode behavior
 * - Filesystem timestamp precision issues (FAT/exFAT)
 * - Edge cases with simultaneous entry creation
 * - Special characters and strings in entry data
 * - Multiple consecutive merge cycles
 * - Interaction between deletions and new entries
 * - Nested group structures
 * - Extended divergence periods
 */
class TestMergeRoundTrip : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Core round-trip tests
    void testBasicRoundTripMerge();
    void testRoundTripWithSynchronizeMode();
    void testRoundTripWithKeepNewerMode();
    void testMultipleRoundTrips();

    // Timestamp and filesystem-related tests
    void testFATTimestampPrecision();
    void testEntriesAtSameTimestamp();
    void testLargeDivergenceWindow();

    // Edge cases
    void testSpecialCharacterEntries();
    void testRoundTripWithDeletions();
    void testEntryModificationsDuringDivergence();
    void testEmptyDatabaseMerge();
    void testNestedGroupsMerge();

    // Merge integrity tests
    void testSourceDatabaseNotModified();

    // Regression tests for deletion propagation bug
    void testDeletionMarkerTimestampEquality();
    void testEntryReaddedAfterDeletion();

    // Issue 2: Entry orphaning investigation tests
    void testDifferentDatabaseUUIDs();
    void testMergeWithGroupMismatch();
    void testEntriesRecoverableAfterMerge();
    void testBidirectionalMergeWithKeepNewer();

private:
    /**
     * Create a base database with a root group and one child group named "Passwords"
     * @return Newly allocated Database (caller takes ownership)
     */
    Database* createBaseDatabase();

    /**
     * Create a deep clone of a database
     * @param source Database to clone
     * @return Newly allocated Database clone (caller takes ownership)
     */
    Database* cloneDatabase(Database* source);

    /**
     * Add an entry to a database
     * @param db Target database
     * @param title Entry title
     * @param password Entry password
     * @param group Target group (nullptr = first child group or root)
     * @return Pointer to created entry
     */
    Entry* addEntry(Database* db, const QString& title, const QString& password, Group* group = nullptr);

    /**
     * Find an entry by title (recursive search)
     * @param db Database to search
     * @param title Title to find
     * @return Entry pointer or nullptr if not found
     */
    Entry* findEntryByTitle(Database* db, const QString& title);

    /**
     * Count all entries in database recursively
     * @param db Database to count
     * @return Number of entries
     */
    int countEntries(Database* db);
};

#endif // KEEPASSX_TESTMERGEROUNDTRIP_H
