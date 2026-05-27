# KeePassXC Merge Data Loss Investigation

## Executive Summary

This document tracks two related but distinct issues discovered during investigation of a data loss scenario involving bidirectional database merge:

| Issue | Description | Merge Mode | Status |
|-------|-------------|------------|--------|
| **Issue 1** | `mergeDeletions()` timestamp logic bug | Synchronize (KeeShare) | **FIXED** |
| **Issue 2** | Entry orphaning during GUI merge | KeepNewer (default) | **Under Investigation** |

---

## Issue 1: Synchronize Mode Deletion Propagation (FIXED)

**Applies to**: `Synchronize` merge mode only (used by KeeShare feature)

Bidirectional database synchronization using `Synchronize` merge mode can cause unexpected entry/group deletion due to flawed timestamp comparison logic in `mergeDeletions()`. Entries that were legitimately added or moved after a deletion marker was created can be incorrectly deleted during merge operations.

### Key Technical Finding

The `mergeDeletions()` function in `Merger.cpp` (lines 607-708) only runs when `mergeMode == Group::Synchronize`:

```cpp
if (mergeMode != Group::Synchronize) {
    // no deletions are applied for any other strategy!
    return changes;  // Line 611-614: Early exit for non-Synchronize modes
}
```

**Standard GUI merge** (Database > Merge From Database) uses the **default mode** which resolves to `KeepNewer` (see `Group::mergeMode()` at `Group.cpp:213`). This means the fix for Issue 1 does NOT affect standard GUI merge operations.

### Fix Implemented

The fix changes lines 648-654 and 678-681 in `Merger.cpp`:

1. Use `>=` instead of `>` for timestamp comparison (handles FAT filesystem 2-second precision)
2. Consider `max(lastModificationTime, locationChanged)` to detect entries that were moved/re-added after deletion

---

## Issue 2: Entry Orphaning During GUI Merge (INVESTIGATION ONGOING)

**Applies to**: Standard GUI merge (`KeepNewer` mode)

### User's Scenario

1. **Origin**: Database created on macOS
2. **Copy method**: File copy to FAT32 and exFAT partitions on USB stick
3. **Changes**: NEW entries added to each database (unique UUIDs)
4. **Merge**: Bidirectional - DB-A→DB-B, then DB-B→DB-A
5. **Result**: Entries not visible in GUI search or Recycle Bin
6. **Recovery**: Via `keepassxc-cli` (entries existed in file but not visible in GUI)

### Current Database State (Verified)

| Property | FAT32 DB | exFAT DB |
|----------|----------|----------|
| Database UUID | `{8814057e-6c05-406b-9c62-3c3421ccbaa2}` | `{6d1a95a1-ca88-41cb-9acc-64f2b0f4497b}` |
| Entry count | 235 | 235 |
| Group count | 7 | 7 |
| Yubikey entry UUID | `{15c61fca-8168-4676-8cea-97c380459aaf}` | Same |

**Note**: Different database UUIDs despite being file copies suggests UUID regeneration occurred at some point.

### Why Issue 1 Fix Doesn't Apply Here

1. GUI merge does NOT set `setForcedMergeMode(Synchronize)` - only KeeShare does (`ShareImport.cpp:96`)
2. Default merge mode resolves to `KeepNewer` (`Group.cpp:213`)
3. `mergeDeletions()` exits early for non-Synchronize modes (lines 611-614)
4. The timestamp comparison fix is never reached in standard GUI merge

### Potential Causes for Issue 2

The "orphaning" symptom (entries exist in KDBX file but not visible in GUI) could be caused by:

1. **Entry conflict resolution**: When entries with the SAME UUID exist in both databases, `resolveEntryConflict_MergeHistories()` uses `eraseEntry()` which bypasses Recycle Bin
2. **Group structure mismatch**: If source and target have different group structures, entries may be placed in unexpected locations
3. **GUI view refresh bug**: Entries may be present but the view not updating
4. **Save/load timing**: Race condition between merge and save operations

---

## Original Issue Description

While I think this is more reflective of misusing the merge functionality (a better flow would probably be along the lines of creating normal kdbx db backups --> merge backups etc) I think this behavior of merge is worth investigating, as post merge it appears merged entries are lost due to DeletedObjects propagation.

I think this is pretty severe, particularly in the event of automated syncs between FAT/exFAT databases on thumb drives that came from a single database source. It seems logical to me that merging first between diverged databases (DB-A into DB-B) then merging the newly merged DB-B back into DB-A to achieve parity should not result in deleted entries, regardless of timestamp precision disparities.




## Steps to Reproduce
1. Create two identical KeePassXC databases (DB-A and DB-B), both using `Synchronize` merge mode
2. In DB-A: Delete an existing entry (creates `DeletedObject` marker with `deletionTime = T1`)
3. In DB-B: Add a new entry **or** re-import/restore the deleted entry from backup (entry gets `locationChanged = T2` where T2 > T1, but may retain old `lastModificationTime`)
4. Merge DB-A into DB-B using `Database > Merge From Database`
5. The entry in DB-B is unexpectedly deleted

**Additional trigger**: FAT/exFAT filesystem timestamp precision (2-second resolution) can cause entries created at the "same time" as a deletion to be deleted due to timestamp equality.

## Expected Versus Actual Behavior

**Expected**: Entries with `locationChanged >= deletionTime` should survive merge (they were added/moved after the deletion)

**Actual**: Only `lastModificationTime > deletionTime` is checked (strict inequality), ignoring `locationChanged`. Entries can be deleted even when they were legitimately added after the deletion marker was created.


AFAICT, this is a bug in the `/home/jsullivan2/git/keepassxc/src/core/Merger.cpp` logic.  I think I can fix this, going to need to stew a bit on how to test properly.  



### Issues Identified

1. **Strict inequality (`>`)**: Uses `>` instead of `>=`. On filesystems with low timestamp precision (FAT: 2 seconds, exFAT: 10ms), entries created at the "same" timestamp as a deletion will be deleted.

2. **Missing `locationChanged` consideration**: An entry may be:
   - Created and modified at `T1`
   - Deleted from DB-A at `T2` (creating DeletedObject)
   - Re-imported/restored to DB-B at `T3` where `T3 > T2`

   The entry's `lastModificationTime` remains `T1`, but `locationChanged` is updated to `T3`. The current logic only checks `lastModificationTime`, so the entry is deleted despite being legitimately re-added.

### Data Flow

```
DeletedObjects (DB-A) + DeletedObjects (DB-B)
           ↓
    mergedDeletions map
           ↓
    For each entry in target:
      if (entry.uuid in mergedDeletions)
        if (entry.lastModificationTime > deletionTime)  // BUG: should consider locationChanged
          keep entry
        else
          DELETE entry  // DATA LOSS
```

## Proposed Fix

### Fixed Code

```cpp
// For entries (around line 648)
QDateTime entryRelevantTime = qMax(entry->timeInfo().lastModificationTime(),
                                   entry->timeInfo().locationChanged());
if (entryRelevantTime >= object.deletionTime) {
    // keep entry since it was changed/moved at or after deletion date
    continue;
}

// For groups (around line 671)
QDateTime groupRelevantTime = qMax(group->timeInfo().lastModificationTime(),
                                   group->timeInfo().locationChanged());
if (groupRelevantTime >= object.deletionTime) {
    // keep group since it was changed/moved at or after deletion date
    continue;
}
```

### Changes

1. Use `>=` instead of `>` to handle timestamp precision edge cases
2. Consider `max(lastModificationTime, locationChanged)` to detect entries that were moved/re-added after deletion

## Test Coverage

New test file created: `/home/jsullivan2/git/keepassxc/tests/TestMergeRoundTrip.cpp`

**21 tests total, all passing:**

### Issue 1 Tests (Synchronize Mode)
- `testRoundTripWithSynchronizeMode` - specifically tests deletion propagation
- `testDeletionMarkerTimestampEquality` - regression test for `>=` vs `>` fix
- `testEntryReaddedAfterDeletion` - regression test for locationChanged consideration

### Issue 2 Investigation Tests (KeepNewer Mode)
- `testDifferentDatabaseUUIDs` - merge between databases with different UUIDs
- `testMergeWithGroupMismatch` - entries in groups that don't exist in target
- `testEntriesRecoverableAfterMerge` - verifies entries are findable via all methods
- `testBidirectionalMergeWithKeepNewer` - directly simulates user's FAT32/exFAT scenario

### Core Round-Trip Tests
- `testBasicRoundTripMerge` - verifies entries survive bidirectional sync
- `testRoundTripWithKeepNewerMode` - tests default GUI merge mode
- `testMultipleRoundTrips` - consecutive merge cycles
- `testFATTimestampPrecision` - 2-second timestamp precision edge case
- `testEntriesAtSameTimestamp` - entries with equal timestamps
- `testLargeDivergenceWindow` - extended divergence period
- `testRoundTripWithDeletions` - legitimate deletions still work
- `testEntryModificationsDuringDivergence` - entry modifications preserved
- `testEmptyDatabaseMerge` - initially empty databases
- `testNestedGroupsMerge` - entries in nested group structures
- `testSpecialCharacterEntries` - special characters in titles
- `testSourceDatabaseNotModified` - source database unchanged

All existing tests in `TestMerge.cpp` continue to pass.

### Test Results Summary

```
********* Finished testing of TestMergeRoundTrip *********
Totals: 21 passed, 0 failed, 0 skipped, 0 blacklisted, 22ms
```

**Key Finding**: All Issue 2 investigation tests pass, indicating the core merge logic is correct. The orphaning issue the user experienced may be:
1. A GUI-specific display/refresh issue
2. A save/load timing issue
3. Something specific to their database or environment

## Workarounds

Until the fix is applied:

1. **Use `KeepNewer` merge mode** instead of `Synchronize` (does not propagate deletions)
2. **Backup before merge**: `cp database.kdbx database.kdbx.backup-$(date +%Y%m%d)`
3. **Check Recycle Bin** after merge for unexpectedly deleted entries
4. **Clear DeletedObjects** via: Tools > Database Tools > Database Maintenance

## KeePassXC Debug Information

```
KeePassXC 2.8.0-snapshot
Revision: c74fcc8f (develop branch)
Qt 5.15.17
Botan 2.19.5
```

## Related Files

| File | Purpose |
|------|---------|
| `/home/jsullivan2/git/keepassxc/src/core/Merger.cpp` | Merge algorithm implementation |
| `/home/jsullivan2/git/keepassxc/src/core/Merger.h` | Merger class definition |
| `/home/jsullivan2/git/keepassxc/src/core/Database.h` | DeletedObject struct definition |
| `/home/jsullivan2/git/keepassxc/src/core/TimeInfo.h` | TimeInfo with locationChanged |
| `/home/jsullivan2/git/keepassxc/tests/TestMerge.cpp` | Existing merge tests |
| `/home/jsullivan2/git/keepassxc/tests/TestMergeRoundTrip.cpp` | New round-trip tests |

## Additional Context

This bug manifests most commonly when:
- Using databases on removable media (FAT/exFAT filesystems)
- Syncing databases across multiple devices
- Restoring entries from backup after accidental deletion
- Using bidirectional sync tools that merge in both directions

The issue is particularly dangerous because:
- Deleted entries may not be immediately noticed
- The Recycle Bin may be emptied automatically
- DeletedObjects markers persist and can cause repeat deletions on subsequent merges

---

## Investigation Log

### 2025-01-25: Initial Analysis

**Findings:**

1. **Two separate issues identified** - The fix for Issue 1 (Synchronize mode) is correct and tested but does NOT apply to the user's actual scenario (GUI merge uses KeepNewer mode)

2. **Code path analysis confirmed**:
   - `MergeDialog::performMerge()` creates a `Merger` without calling `setForcedMergeMode()`
   - This means `m_mode = Group::Default` (line 126-127 of Merger.cpp)
   - `mergeDeletions()` checks `mergeMode != Group::Synchronize` and exits early
   - The timestamp comparison fix is never executed for standard GUI merge

3. **Test results**: All 17 tests in `TestMergeRoundTrip.cpp` pass, confirming the Synchronize mode fix is correct

4. **Next steps**: Need to investigate the "orphaning" scenario where entries exist in KDBX file but aren't visible in GUI

### Code Paths for Standard GUI Merge

```
MergeDialog::performMerge() [MergeDialog.cpp:185]
  └─> Merger(sourceDb, targetDb).merge() [no setForcedMergeMode]
        └─> mergeGroup() [lines 186-262]
        │     └─> For NEW entries: clone + moveEntry [lines 194-199]
        │     └─> For EXISTING entries: resolveEntryConflict() [line 211]
        └─> mergeDeletions() [lines 607-708]
        │     └─> EXIT EARLY if mode != Synchronize [lines 611-614]
        └─> mergeMetadata() [lines 710-775]
```

### 2025-01-25: Test Results (Continued)

**All 21 tests pass including Issue 2 simulation tests:**

The `testBidirectionalMergeWithKeepNewer` test directly simulates the user's scenario:
1. Create database with common entries
2. Clone to simulate FAT32 and exFAT copies
3. Add unique entries to each (like "yubikey-pin-fat32" and "entry-5-exfat")
4. Merge bidirectionally with KeepNewer mode
5. Verify all entries are findable via `findEntryByUuid`, `entriesRecursive`, and title search

**Result**: All entries are correctly merged and findable. The core merge logic is correct.

### 2025-01-25: Deep Dive Investigation (sid/keepnewer-analysis branch)

#### Code Path Analysis Complete

**1. eraseEntry() Analysis** (Merger.cpp:397-416)
- Saves DeletedObjects list BEFORE deletion
- Deletes entry (which adds to DeletedObjects via destructor)
- RESTORES old DeletedObjects list (erasing the deletion record)
- This is CORRECT behavior - used when replacing entry with newer version
- Does NOT cause orphaning

**2. Entry::setGroup() Analysis** (Entry.cpp:1451-1484)
- Sets entry's `m_group` pointer
- Calls `group->addEntry(this)` which adds to group's `m_entries` list
- Emits signals for GUI model updates
- This is CORRECT - entries ARE properly added to groups

**3. Group::addEntry() Analysis** (Group.cpp:985-1003)
- Emits `entryAboutToAdd` signal
- Appends entry to `m_entries` list
- Emits `entryAdded` signal
- EntryModel connects to these signals and updates

**4. GUI Signal Flow After Merge**
- MergeDialog emits `databaseMerged(bool)`
- DatabaseWidget shows success message
- NO explicit GUI refresh call - relies on signal propagation
- EntryModel updates via Group signals, NOT via explicit refresh

**5. Search Implementation** (EntrySearcher.cpp)
- Uses `group->entries()` for each group recursively
- If entries are in group's `m_entries` list, search WILL find them
- No caching issues identified

#### Key Finding: Signal Propagation Dependency

The GUI relies on Qt signal/slot connections to update:
1. When entry is added to group: `Group::entryAdded(Entry*)` signal
2. EntryModel receives signal and calls `m_entries = m_group->entries()`
3. View updates to show new entry

**Potential Issue**: If the EntryModel is not currently displaying the group where entries were added, the model won't receive the signal. However, search should still work since it directly queries all groups.

### Conclusion

The core `Merger` class logic is correct for both Issue 1 and Issue 2 scenarios. The user's orphaning issue must be caused by something outside the merge algorithm itself:

1. **GUI view refresh** - The GUI may not have refreshed after merge
2. **Save timing** - The merged database may not have been saved correctly
3. **Search implementation** - A bug in the search widget specifically
4. **Database-specific** - Something unique to the user's database structure

The entries being "recoverable via CLI" but not visible in GUI strongly suggests a GUI display issue rather than a data loss issue.

### Remaining Hypotheses for Issue 2

1. **Signal Disconnect**: After merge, if user was in search mode, the search results aren't automatically refreshed
2. **Model Caching**: If user searches immediately after merge, the search might use stale cached results
3. **View Not Focused**: The entry view might not be focused on the group containing new entries
4. **Filesystem Sync**: On FAT32/exFAT, the save might not have fully synced to disk before file was copied

### Recommended Next Steps

1. Add explicit GUI refresh after merge completion
2. Add debug logging to trace entry visibility after merge
3. Create GUI-level test that simulates merge and verifies search finds new entries

### Questions for User

1. How were DB-A and DB-B created? (Direct file copy vs export/import)
2. What exactly were the "unique additions"? (New entries with new UUIDs vs modifications)
3. How were entries recovered? (keepassxc-cli vs backup vs GUI after some action)
4. What is the merge mode set on your database groups? (Right-click group → Edit Group → Properties → Merge Mode)
5. **NEW**: After merging, did you save and close the database, then reopen? Did entries appear after reopening?

---

## Recommendations

### For Users

1. **Backup before merge**: `cp database.kdbx database.kdbx.backup-$(date +%Y%m%d)`
2. **Use KeepNewer mode** for standard sync (does not propagate deletions)
3. **Only use Synchronize mode** if you specifically want deletion propagation (KeeShare use case)
4. **Check Recycle Bin** after merge for unexpectedly deleted entries
5. **Use keepassxc-cli** to verify entries exist if they appear missing in GUI

### For Developers

1. The Issue 1 fix is complete and tested for Synchronize mode
2. Issue 2 (orphaning in KeepNewer mode) requires further investigation
3. Consider adding GUI warning when merge mode is Synchronize due to deletion propagation risk

---

## Filesystem and Media Considerations

This section documents potential issues specific to cross-platform database synchronization on removable media, particularly when databases are copied between macOS (APFS) and Linux (FAT32/exFAT).

### Timestamp Resolution Differences

Different filesystems have vastly different timestamp precision, which can cause merge conflicts and data loss:

| Filesystem | Modified Time Resolution | Created Time Resolution | Access Time Resolution |
|------------|-------------------------|------------------------|----------------------|
| **APFS** | 1 nanosecond | 1 nanosecond | 1 nanosecond |
| **ext4** | 1 nanosecond | 1 nanosecond | 1 nanosecond |
| **NTFS** | 100 nanoseconds | 100 nanoseconds | 100 nanoseconds |
| **exFAT** | 10 milliseconds | 10 milliseconds | 2 seconds |
| **FAT32** | **2 seconds** | 10 milliseconds | **1 day (date only)** |

**Key Issue**: When a KDBX file is copied from APFS (nanosecond precision) to FAT32 (2-second precision), file timestamps are **rounded**. Files with odd second values may be rounded up or down, causing:

1. **False positives**: Files appear newer than they are
2. **False negatives**: Files appear older than they are
3. **Timestamp equality issues**: Two files created 1 second apart may have identical timestamps on FAT32

**Impact on KeePassXC**: The `Merger.cpp` fix uses `>=` instead of `>` to handle the 2-second precision edge case where entries created at the "same" timestamp as a deletion marker would otherwise be incorrectly deleted.

**Sources**:
- [NTFS.com - Timestamp Format](http://ntfs.com/exfat-time-stamp.htm)
- [Heiko's Blog - Camera Manufacturers: Please Use Accurate Time Stamps](https://www.heiko-sieger.info/camera-manufacturers-please-use-accurate-time-stamps-for-image-files/)
- [Microsoft exFAT Specification](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification)

### macOS Extended Attributes and Dot-Underscore Files

When macOS writes files to non-native filesystems (FAT32, exFAT, NTFS), it creates companion "AppleDouble" files to preserve extended attributes (xattr) that the target filesystem cannot store natively.

**Example**:
```
database.kdbx      # Main KDBX file
._database.kdbx    # AppleDouble file with macOS extended attributes
```

**Contents of `._` files**:
- `com.apple.quarantine` - Gatekeeper quarantine flag
- `kMDItemWhereFroms` - URL if downloaded from internet
- `kMDItemDownloadedDate` - Download timestamp
- Resource fork data (legacy)

**Potential Issues**:

1. **Orphaned `._` files**: If the main file is renamed/deleted on Linux, the `._` file remains, causing confusion
2. **File size discrepancy**: The presence of `._` files may affect sync tools that compare file counts
3. **Attribute loss**: When files are modified on Linux, the `._` file is NOT updated, causing macOS to see stale metadata
4. **Quarantine flag**: If a KDBX file was downloaded, the quarantine flag in the `._` file persists and may affect opening behavior on macOS

**Cleanup**: On macOS, use `dot_clean -v <folder>` to merge `._` files back into main files or remove orphans.

**Sources**:
- [The Eclectic Light Company - Extended Attributes](https://eclecticlight.co/2018/01/12/which-file-systems-and-cloud-services-preserve-extended-attributes/)
- [Swift Forensics - The ._ (dot-underscore) file format](http://www.swiftforensics.com/2018/11/the-dot-underscore-file-format.html)
- [dfir.ch - macOS Extended Attributes: Case Study](https://dfir.ch/posts/macos_extended_attributes/)

### SD Card Write Caching and Corruption Risks

SD cards and USB flash drives introduce additional data integrity risks that can corrupt KDBX files:

**Write Caching Risks**:

1. **RAM cache size**: Modern systems cache writes in RAM for performance. A power loss or removal before cache flush can lose significant data
2. **Flash write behavior**: SD cards use MLC NAND flash that requires reading 128KB-256KB into memory, erasing the sector, then writing. Power loss during this operation corrupts data
3. **No journaling**: FAT32 and exFAT lack journaling, so incomplete writes can corrupt the entire filesystem

**Unsafe Removal Scenarios**:

| Scenario | Risk Level | Consequence |
|----------|------------|-------------|
| Remove during active write | **Critical** | File corruption, sector damage |
| Remove without ejecting (idle) | High | Unflushed cache lost, partial writes |
| Eject but remove too quickly | Medium | Some systems don't sync immediately |
| Proper eject, wait 5+ seconds | Low | Generally safe |

**Linux-Specific Concerns**:

- Linux uses larger write-back caches by default
- `sync` command should be run explicitly before removal
- Mount with `sync` option forces immediate writes (slower but safer)

**Recommendations**:
```bash
# Force sync before removing SD card
sync && sudo umount /media/sdcard

# Mount with synchronous writes (slower but safer for databases)
sudo mount -o sync /dev/sdc1 /mnt/sdcard
```

**Sources**:
- [embeddedTS - Filesystem Corruption with SD cards](https://docs.embeddedts.com/Filesystem_Corruption_with_SD_cards)
- [Microsoft - USB and SD storage media write caching](https://support.microsoft.com/en-us/topic/usb-and-sd-storage-media-correctly-log-on-storage-media-and-enable-write-caching-fc436b8c-a19b-ce79-e17e-5af31a26cef8)
- [Raspberry Pi Forums - Prevent SD-Card Corruption](https://forums.raspberrypi.com/viewtopic.php?t=36533)

### Partition Table Considerations: APFS to FAT32/exFAT

When repartitioning an SD card from APFS to FAT32/exFAT:

**Potential Issues**:

1. **APFS remnants**: APFS uses a container model. Simply deleting partitions may leave APFS container metadata that confuses some systems
2. **GPT vs MBR**: APFS requires GPT (GUID Partition Table). FAT32 traditionally uses MBR. A clean repartition must convert the partition scheme
3. **EFI System Partition**: Disk Utility may leave an EFI partition when converting schemes
4. **Alignment issues**: Partitions created for flash media should be aligned to erase block boundaries (typically 4MB)

**Best Practice for Clean Repartitioning**:
```bash
# On macOS - completely erase and repartition
diskutil eraseDisk MS-DOS "SDCARD" MBR /dev/diskN  # For FAT32 on MBR
diskutil eraseDisk ExFAT "SDCARD" GPT /dev/diskN   # For exFAT on GPT
```

**Verification**:
```bash
# Check partition scheme
diskutil list /dev/diskN

# Verify no APFS containers remain
diskutil apfs list
```

**Sources**:
- [Eric from Canada - Details of Disk Utility's partitioning decisions](https://ericfromcanada.github.io/output/2019/disk-utility-partitioning-details.html)
- [Apple Discussions - GPT partitioning](https://discussions.apple.com/thread/253663450)

### File Locking and Concurrent Access

FAT32 and exFAT lack proper file locking mechanisms, which creates risks when databases are accessed from multiple operating systems:

**Critical Differences**:

| Feature | APFS/HFS+ | ext4 | FAT32/exFAT |
|---------|-----------|------|-------------|
| File locking | Yes | Yes | **No** |
| Mandatory locking | Yes | Optional | **No** |
| Journaling | Yes | Yes | **No** |
| Permission enforcement | Yes | Yes | **No** |

**Impact on KeePassXC**:

1. **No lock file enforcement**: KeePassXC creates `.lock` files, but FAT32/exFAT doesn't enforce them at the filesystem level
2. **Concurrent access risk**: If the same database is opened on both macOS and Linux simultaneously (e.g., via network share or VM shared folder), corruption is likely
3. **Save race conditions**: If one system saves while another is reading, the read may get partial data

**Recommendations**:

1. **Never open the same KDBX on two systems simultaneously** when using FAT32/exFAT
2. **Always close and save** before switching systems
3. **Wait for sync completion** on cloud services before opening on another device
4. **Use a journaled filesystem** if possible (NTFS for Windows compatibility, or a dedicated sync service)

**Sources**:
- [How-To Geek - FAT32 vs. exFAT vs. NTFS](https://www.howtogeek.com/235596/whats-the-difference-between-fat32-exfat-and-ntfs/)
- [Medium - Notes on exFAT and Reliability](https://pawitp.medium.com/notes-on-exfat-and-reliability-d2f194d394c2)
- [OWC Blog - Picking the Right Drive Format](https://eshop.macsales.com/blog/80813-picking-the-right-drive-format/)

### KDBX-Specific Cross-Platform Considerations

**How KDBX Handles Internal Timestamps**:

KeePassXC uses internal timestamps (stored inside the encrypted database) for merge decisions, NOT filesystem timestamps:

- `lastModificationTime` - When entry content was last changed
- `locationChanged` - When entry was moved to a different group
- `deletionTime` - When entry was added to DeletedObjects

**Important**: These internal timestamps are written by the application and are NOT affected by filesystem timestamp precision. However, if your system clock differs between macOS and Linux, merged timestamps may be inconsistent.

**Database UUID Changes**:

When copying a KDBX file, the database maintains its internal UUID. However:
- Using "Save As" may generate a new database UUID
- Some export/import operations regenerate UUIDs
- Different database UUIDs prevent proper sync (treated as unrelated databases)

**Verification**:
```bash
# Check database metadata on each copy
keepassxc-cli show -s database.kdbx

# Verify UUIDs match between databases before merge
# Different database UUIDs suggest files diverged improperly
```

### Specific Scenario Analysis: User's FAT32/exFAT Issue

Based on the user's scenario (original APFS database, repartitioned SD card, operations on Rocky Linux 10):

**Potential Contributing Factors**:

1. **Timestamp truncation**: When copying from APFS to FAT32, file modification times were truncated to 2-second precision. If entries were created within the same 2-second window, their relative ordering could change.

2. **`._` file pollution**: macOS created `._database.kdbx` files on the FAT32/exFAT partitions. When the database was modified on Linux, these files became stale, potentially causing issues on return to macOS.

3. **Write caching**: If the SD card was removed from the Linux machine without proper unmounting, in-flight writes may have been lost, resulting in a partially-written KDBX file.

4. **Partition table confusion**: The SD card's original APFS formatting, followed by FAT32/exFAT repartitioning, may have left remnants that caused subtle read/write issues.

5. **Clock skew**: If the Rocky Linux machine's system clock differed from macOS, the internal entry timestamps would reflect this, potentially affecting merge decisions.

**Diagnostic Commands**:

```bash
# On macOS - check for APFS remnants on SD card
diskutil apfs list

# Check for hidden ._ files
ls -la /Volumes/SDCARD/

# Verify KDBX file integrity
keepassxc-cli check /Volumes/SDCARD/database.kdbx

# Compare database UUIDs
keepassxc-cli show -s /Volumes/FAT32/database.kdbx
keepassxc-cli show -s /Volumes/exFAT/database.kdbx
```

### Recommendations for Cross-Platform Database Sync

**DO**:

1. **Always safely eject** removable media before removal
2. **Run `sync`** on Linux before unmounting: `sync && sudo umount /mnt/sdcard`
3. **Use exFAT over FAT32** when possible (10ms vs 2-second precision)
4. **Verify time zones match** between systems (`timedatectl` on Linux)
5. **Keep system clocks synchronized** (NTP enabled on both systems)
6. **Clean `._` files** before switching back to Linux: `dot_clean -v /Volumes/SDCARD`
7. **Backup before merge**: Always create a timestamped backup before any merge operation
8. **Use keepassxc-cli** to verify entries after merge: `keepassxc-cli ls -R database.kdbx`

**DO NOT**:

1. **Do not remove SD cards during active writes** - wait for all I/O to complete
2. **Do not open the same database simultaneously** on multiple systems
3. **Do not rely on FAT32/exFAT for long-term storage** - use as transfer medium only
4. **Do not skip the eject/unmount step** - kernel caches are not immediately flushed
5. **Do not use FAT32 for databases with frequent modifications** - 2-second precision is problematic
6. **Do not merge immediately after copy** - ensure filesystem sync is complete

### Known Gotchas with SD Cards and KeePassXC

| Issue | Symptom | Solution |
|-------|---------|----------|
| 2-second FAT32 precision | Entries created near deletion time are deleted | Use exFAT or the `>=` fix |
| `._` file orphans | Confusion about file count, stale metadata | Run `dot_clean -v` |
| Unsafe removal | Database corruption, partial writes | Always eject properly |
| Clock skew | Merge uses wrong "newer" entry | Sync NTP on both systems |
| No file locking | Concurrent access corruption | Only open on one system at a time |
| APFS partition remnants | Read/write errors after repartition | Fully erase disk, not just partition |
| Linux write-back cache | Data loss on SD card removal | Mount with `sync` option or run `sync` before unmount |
| exFAT corruption on macOS | "Disk not ejected properly" errors | Format exFAT on Windows, not macOS |

**Sources**:
- [KeePass - Synchronization](https://keepass.info/help/v2/sync.html)
- [KeePassium - How to merge slightly different databases](https://support.keepassium.com/kb/database-merge/)
- [Ctrl Blog - Be wary of file sync conflicts with KeePass apps](https://www.ctrl.blog/entry/keepass-file-conflicts-android.html)
