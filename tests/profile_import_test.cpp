// Offline regression for the production import helper. No Settings construction,
// AppData lookup, registry access, application startup, or audio-device access.
// Build with /FC so __FILE__ anchors all fixtures to this checkout's tests/bin.
#include "../fxsound/Source/Utils/Settings/SettingsImport.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <Windows.h>
#include <winioctl.h>

static_assert(JUCE_VERSION == 0x060106, "Use the application's JUCE 6.1.6 sources.");

static void require(bool ok, const char* message)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void writeFixture(const juce::File& root, const char* relativePath, const char* contents)
{
    const auto file = root.getChildFile(relativePath);
    require(file.isAChildOf(root), "fixture write stays under its profile root");
    require(file.getParentDirectory().createDirectory().wasOk(), "create fixture parent");
    require(file.replaceWithText(contents), "write fixture file");
}

// Include every directory and every file's exact bytes and last-write time, so
// preservation assertions catch overwrites, additions, removals, and retouching.
using Snapshot = std::map<std::string, std::pair<std::string, juce::int64>>;

static Snapshot snapshot(const juce::File& root)
{
    Snapshot result;
    for (const auto& file : root.findChildFiles(juce::File::findFilesAndDirectories, true))
    {
        const auto relative = file.getRelativePathFrom(root).toStdString();
        if (file.isDirectory())
            result.emplace(relative + "/", std::make_pair(std::string{}, juce::int64{}));
        else
        {
            juce::MemoryBlock data;
            require(file.loadFileAsData(data), "read fixture snapshot");
            const std::string bytes(static_cast<const char*>(data.getData()), data.getSize());
            result.emplace(relative, std::make_pair(bytes, file.getLastModificationTime().toMilliseconds()));
        }
    }
    return result;
}

static void makeStockProfile(const juce::File& source)
{
    writeFixture(source, "FxSound.settings", "<PROPERTIES><VALUE name=\"preset\" val=\"Fixture\"/></PROPERTIES>");
    writeFixture(source, "Presets/Fixture.fac", "saved preset bytes\n");
    writeFixture(source, "Presets/Nested/Other.fac", "nested preset bytes\n");
    writeFixture(source, "AutoSave/Fixture.fac", "unsaved edits differ from the saved preset\n");
    writeFixture(source, "FxSound.secure", "fixture-only data that must not be imported\n");
}

static void makeFixtureJunction(const juce::File& run, const juce::File& link, const juce::File& target)
{
    require(link.isAChildOf(run) && target.isAChildOf(run), "junction endpoints stay in this fixture run");
    require(!link.exists() && target.isDirectory(), "junction fixture uses a fresh link and existing target");
    require(link.createDirectory().wasOk(), "create junction fixture directory");
    // Mount-point junctions require no symlink privilege and change no ACL.
    struct JunctionData
    {
        DWORD tag;
        WORD dataLength, reserved, substituteOffset, substituteLength, printOffset, printLength;
        wchar_t paths[8192];
    } data = {};
    const std::wstring printName(target.getFullPathName().toWideCharPointer());
    const auto substituteName = std::wstring(L"\\??\\") + printName;
    const auto characterCount = substituteName.size() + printName.size() + 2;
    require(characterCount < 8192 && characterCount * sizeof(wchar_t) + 16 <= MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
            "junction fixture path fits reparse buffer");
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substituteLength = static_cast<WORD>(substituteName.size() * sizeof(wchar_t));
    data.printOffset = static_cast<WORD>((substituteName.size() + 1) * sizeof(wchar_t));
    data.printLength = static_cast<WORD>(printName.size() * sizeof(wchar_t));
    data.dataLength = static_cast<WORD>(8 + characterCount * sizeof(wchar_t));
    std::copy(substituteName.begin(), substituteName.end(), data.paths);
    std::copy(printName.begin(), printName.end(), data.paths + substituteName.size() + 1);
    const auto handle = CreateFileW(link.getFullPathName().toWideCharPointer(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "open fixture junction handle");
    DWORD returnedBytes = 0;
    const auto created = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data, data.dataLength + 8,
                                         nullptr, 0, &returnedBytes, nullptr);
    require(CloseHandle(handle) != 0, "close fixture junction handle");
    require(created != 0 && link.isSymbolicLink(), "create fixture junction");
}

static juce::Result importFixture(const juce::File& run, const juce::File& source,
                                  const juce::File& destination)
{
    require(source.isAChildOf(run) && destination.isAChildOf(run),
            "both import paths stay within this test run");
    require(source != destination && !source.isAChildOf(destination) && !destination.isAChildOf(source),
            "fixture source and destination are separate directories");
    return FxSound::importStockProfile(source, destination);
}

static void requireImported(const juce::File& source, const juce::File& destination)
{
    for (const auto* relative : { "FxSound.settings", "Presets/Fixture.fac",
                                 "Presets/Nested/Other.fac", "AutoSave/Fixture.fac" })
    {
        require(destination.getChildFile(relative).existsAsFile(), "expected imported file exists");
        require(source.getChildFile(relative).hasIdenticalContentTo(destination.getChildFile(relative)),
                "imported file has exact source bytes");
    }
    require(!destination.getChildFile("FxSound.secure").exists(), "import only settings and preset folders");
    require(destination.findChildFiles(juce::File::findFiles, true).size() == 4,
            "settings commit leaves no temporary file behind");
}

int main()
{
    require(juce::File::isAbsolutePath(__FILE__), "compile this test with MSVC /FC");
    const auto tests = juce::File(__FILE__).getParentDirectory();
    const auto bin = tests.getChildFile("bin");
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    require(executable.getParentDirectory() == bin, "run this executable only from this checkout's tests/bin");
    require(!bin.isSymbolicLink(), "tests/bin cannot redirect fixture writes");
    const auto fixtures = bin.getChildFile("profile-fixtures");
    require(!fixtures.isSymbolicLink(), "profile-fixtures cannot redirect fixture writes");
    require(fixtures.createDirectory().wasOk(), "create fixture root");
    const auto run = fixtures.getChildFile(juce::Uuid().toString());
    require(!run.exists() && run.isAChildOf(fixtures), "use a fresh contained fixture run");
    require(run.createDirectory().wasOk(), "create fixture run");
    std::printf("Fixture root: %s\n", run.getFullPathName().toRawUTF8());

    const auto source = run.getChildFile("first/stock");
    const auto destination = run.getChildFile("first/custom");
    makeStockProfile(source);
    const auto sourceBefore = snapshot(source);
    require(importFixture(run, source, destination).wasOk(), "first import succeeds");
    requireImported(source, destination);
    require(snapshot(source) == sourceBefore, "first import leaves source untouched");
    std::puts("PASS: first import includes saved presets, nested presets, and AutoSave; source unchanged");

    const auto existing = run.getChildFile("existing/custom");
    writeFixture(existing, "FxSound.settings", "custom settings must remain unchanged\n");
    writeFixture(existing, "Presets/Fixture.fac", "custom preset must remain unchanged\n");
    writeFixture(existing, "AutoSave/Fixture.fac", "custom unsaved edits must remain unchanged\n");
    writeFixture(existing, "unrelated/note.txt", "other custom files must remain unchanged\n");
    const auto existingBefore = snapshot(existing);
    require(importFixture(run, source, existing).wasOk(), "existing profile skips import successfully");
    require(snapshot(existing) == existingBefore, "existing settings preserve every custom file");
    require(snapshot(source) == sourceBefore, "skipped import leaves source untouched");
    std::puts("PASS: existing custom settings preserve the entire custom profile");

    const auto absent = run.getChildFile("absent/stock");
    const auto absentDestination = run.getChildFile("absent/custom");
    require(importFixture(run, absent, absentDestination).wasOk(), "absent stock is a successful no-op");
    require(!absent.exists() && !absentDestination.exists(), "absent stock creates no profile");
    writeFixture(absent, "Presets/Orphan.fac", "stock presets without a settings marker\n");
    const auto absentBefore = snapshot(absent);
    require(importFixture(run, absent, absentDestination).wasOk(), "missing stock settings is a successful no-op");
    require(!absentDestination.exists() && snapshot(absent) == absentBefore,
            "stock without a settings marker creates no profile and is unchanged");
    std::puts("PASS: absent stock or absent stock settings performs no import");

    const auto minimalSource = run.getChildFile("minimal/stock");
    const auto minimalDestination = run.getChildFile("minimal/custom");
    writeFixture(minimalSource, "FxSound.settings", "stock settings without optional preset folders\n");
    const auto minimalBefore = snapshot(minimalSource);
    require(importFixture(run, minimalSource, minimalDestination).wasOk(), "missing optional preset folders are allowed");
    require(minimalSource.getChildFile("FxSound.settings").hasIdenticalContentTo(minimalDestination.getChildFile("FxSound.settings")),
            "settings import succeeds without optional preset folders");
    require(snapshot(minimalSource) == minimalBefore, "minimal source remains untouched");
    std::puts("PASS: missing optional preset folders still allow settings import");

    const auto retryDestination = run.getChildFile("retry/custom");
    writeFixture(retryDestination, "Presets", "a regular file deliberately blocks the directory copy\n");
    const auto blockedBefore = snapshot(retryDestination);
    const auto failure = importFixture(run, source, retryDestination);
    require(failure.failed(), "a file blocking the Presets directory fails deterministically");
    require(!retryDestination.getChildFile("FxSound.settings").exists(), "failed copy leaves no completion marker");
    require(snapshot(retryDestination) == blockedBefore, "failed first folder copy preserves the blocker");
    require(snapshot(source) == sourceBefore, "failed import leaves source untouched");
    std::puts("PASS: blocked preset copy fails without committing settings or changing the source");

    const auto blocker = retryDestination.getChildFile("Presets");
    require(blocker.isAChildOf(run) && blocker.existsAsFile(), "remove only the known test blocker");
    require(blocker.deleteFile(), "remove fixture blocker");
    require(importFixture(run, source, retryDestination).wasOk(), "retry succeeds after fixing fixture");
    requireImported(source, retryDestination);
    require(snapshot(source) == sourceBefore, "successful retry leaves source untouched");
    std::puts("PASS: retry after fixture repair imports the complete profile");

    // Deny new directory handles without changing any ACL. All paths are still
    // synthetic fixtures; the handle is closed before checking the outcome.
    const auto lockedSource = run.getChildFile("locked/stock");
    const auto lockedDestination = run.getChildFile("locked/custom");
    makeStockProfile(lockedSource);
    const auto lockedBefore = snapshot(lockedSource);
    const auto lockedDirectory = lockedSource.getChildFile("Presets");
    const auto handle = CreateFileW(lockedDirectory.getFullPathName().toWideCharPointer(), FILE_LIST_DIRECTORY,
                                    0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "open exclusive fixture directory handle");
    const auto lockedResult = importFixture(run, lockedSource, lockedDestination);
    require(CloseHandle(handle) != 0, "close exclusive fixture directory handle");
    require(lockedResult.failed(), "locked source directory fails enumeration");
    require(!lockedDestination.getChildFile("FxSound.settings").exists(), "enumeration failure leaves settings marker absent");
    require(snapshot(lockedSource) == lockedBefore, "enumeration failure leaves source unchanged");
    require(importFixture(run, lockedSource, lockedDestination).wasOk(), "retry after releasing directory lock succeeds");
    requireImported(lockedSource, lockedDestination);
    require(snapshot(lockedSource) == lockedBefore, "enumeration retry leaves source unchanged");
    std::puts("PASS: source enumeration failure leaves no marker; retry after releasing fixture lock succeeds");

    const auto autosaveDestination = run.getChildFile("autosave-failure/custom");
    const auto autosave = source.getChildFile("AutoSave");
    const auto autosaveHandle = CreateFileW(autosave.getFullPathName().toWideCharPointer(), FILE_LIST_DIRECTORY,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(autosaveHandle != INVALID_HANDLE_VALUE, "lock fixture AutoSave directory");
    const auto autosaveResult = importFixture(run, source, autosaveDestination);
    require(CloseHandle(autosaveHandle) != 0, "release fixture AutoSave lock");
    require(autosaveResult.failed(), "AutoSave enumeration fails after Presets copy");
    require(autosaveDestination.getChildFile("Presets/Fixture.fac").existsAsFile(), "Presets copied before AutoSave failure");
    require(!autosaveDestination.getChildFile("FxSound.settings").exists(), "partial import leaves settings marker absent");
    require(snapshot(source) == sourceBefore, "AutoSave failure leaves source unchanged");
    require(importFixture(run, source, autosaveDestination).wasOk(), "retry completes partial import");
    requireImported(source, autosaveDestination);
    std::puts("PASS: AutoSave failure after Presets copy leaves no marker; retry completes import");

    const auto settingsDestination = run.getChildFile("settings-failure/custom");
    const auto sourceSettings = source.getChildFile("FxSound.settings");
    const auto settingsHandle = CreateFileW(sourceSettings.getFullPathName().toWideCharPointer(), GENERIC_READ,
        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(settingsHandle != INVALID_HANDLE_VALUE, "lock fixture settings file");
    const auto settingsResult = importFixture(run, source, settingsDestination);
    require(CloseHandle(settingsHandle) != 0, "release fixture settings lock");
    require(settingsResult.failed(), "unreadable settings fails after preset copy");
    require(settingsDestination.getChildFile("AutoSave/Fixture.fac").existsAsFile(), "AutoSave copied before settings read failure");
    require(!settingsDestination.getChildFile("FxSound.settings").exists(), "settings read failure leaves marker absent");
    require(snapshot(source) == sourceBefore, "settings read failure leaves source unchanged");
    require(importFixture(run, source, settingsDestination).wasOk(), "retry after releasing settings lock succeeds");
    requireImported(source, settingsDestination);
    std::puts("PASS: final settings read failure leaves no marker; retry succeeds");

    const auto rootAlias = run.getChildFile("root-alias/custom");
    makeFixtureJunction(run, rootAlias, source);
    require(importFixture(run, source, rootAlias).failed(), "root junction is rejected even with an existing marker");
    require(snapshot(source) == sourceBefore, "root junction rejection preserves the entire source");
    const auto presetAlias = run.getChildFile("preset-alias/custom");
    makeFixtureJunction(run, presetAlias.getChildFile("Presets"), source.getChildFile("Presets"));
    require(importFixture(run, source, presetAlias).failed(), "destination Presets junction is rejected");
    require(!presetAlias.getChildFile("FxSound.settings").exists(), "preset junction rejection leaves marker absent");
    require(snapshot(source) == sourceBefore, "preset junction rejection never deletes a source preset");
    const auto nestedAlias = run.getChildFile("nested-alias/custom");
    makeFixtureJunction(run, nestedAlias.getChildFile("Presets/Nested"), source.getChildFile("Presets/Nested"));
    require(importFixture(run, source, nestedAlias).failed(), "nested destination junction is rejected");
    require(!nestedAlias.getChildFile("FxSound.settings").exists(), "nested junction rejection leaves marker absent");
    require(snapshot(source) == sourceBefore, "nested junction rejection preserves the entire source");
    std::puts("PASS: root, Presets, and nested destination junctions cannot redirect writes into the source");
    std::puts("PASS: all offline profile-import regressions (fixtures retained under tests/bin/profile-fixtures)");
}
