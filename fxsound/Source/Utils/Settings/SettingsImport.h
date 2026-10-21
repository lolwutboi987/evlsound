/*
FxSound
Copyright (C) 2025 FxSound LLC
SPDX-License-Identifier: AGPL-3.0-or-later
*/
#pragma once

#include <juce_core/juce_core.h>
#include <filesystem>

namespace FxSound
{
namespace ProfileImportDetail
{
inline std::filesystem::path nativePath(const juce::File& file)
{
    return std::filesystem::path(file.getFullPathName().toWideCharPointer());
}

inline juce::Result inspect(const juce::File& file, std::filesystem::file_status& status)
{
    std::error_code error;
    status = std::filesystem::symlink_status(nativePath(file), error);
    if (error == std::errc::no_such_file_or_directory)
        status = std::filesystem::file_status(std::filesystem::file_type::not_found);
    else if (error)
        return juce::Result::fail("Cannot inspect a profile path for import.");
    return juce::Result::ok();
}

inline juce::Result validateDirectory(const juce::File& directory)
{
    std::filesystem::file_status status;
    const auto inspected = inspect(directory, status);
    if (inspected.failed())
        return inspected;
    if (std::filesystem::exists(status)
        && (!std::filesystem::is_directory(status) || directory.isSymbolicLink()))
        return juce::Result::fail("A profile directory is not an independent regular directory.");
    return juce::Result::ok();
}

inline juce::Result validateRoots(const juce::File& source, const juce::File& destination)
{
    std::error_code error;
    const auto sourcePath = std::filesystem::weakly_canonical(nativePath(source), error);
    if (error)
        return juce::Result::fail("Cannot resolve the stock profile path.");
    const auto destinationPath = std::filesystem::weakly_canonical(nativePath(destination), error);
    if (error)
        return juce::Result::fail("Cannot resolve the independent profile path.");
    const juce::File resolvedSource(juce::String(sourcePath.c_str()));
    const juce::File resolvedDestination(juce::String(destinationPath.c_str()));
    if (resolvedSource == resolvedDestination || resolvedSource.isAChildOf(resolvedDestination)
        || resolvedDestination.isAChildOf(resolvedSource))
        return juce::Result::fail("The stock and independent profiles must not overlap.");
    // Canonical comparison permits ordinary redirected user-profile ancestors,
    // while these checks reject redirects within the application's own profile.
    for (const auto& directory : { destination, destination.getChildFile("Presets"), destination.getChildFile("AutoSave") })
    {
        const auto valid = validateDirectory(directory);
        if (valid.failed())
            return valid;
    }
    return juce::Result::ok();
}

inline juce::Result copyDirectory(const juce::File& source, const juce::File& destination)
{
    const auto validDestination = validateDirectory(destination);
    if (validDestination.failed())
        return validDestination;
    if (destination.createDirectory().failed())
        return juce::Result::fail("Cannot create an evlsound preset directory.");

    // JUCE's copyDirectoryTo silently treats an enumeration failure as an empty
    // directory. Keep explicit errors so incomplete imports never get a marker.
    std::error_code error;
    std::filesystem::directory_iterator entry(nativePath(source), error);
    if (error)
        return juce::Result::fail("Cannot enumerate FxSound presets for import.");

    const std::filesystem::directory_iterator end;
    while (entry != end)
    {
        const auto status = entry->symlink_status(error);
        if (error)
            return juce::Result::fail("Cannot inspect a FxSound preset for import.");

        const juce::File sourceEntry(juce::String(entry->path().c_str()));
        const auto destinationEntry = destination.getChildFile(sourceEntry.getFileName());
        if (sourceEntry.isSymbolicLink())
            return juce::Result::fail("A stock preset path redirects outside its directory.");
        if (std::filesystem::is_directory(status))
        {
            const auto copied = copyDirectory(sourceEntry, destinationEntry);
            if (copied.failed())
                return copied;
        }
        else
        {
            std::filesystem::file_status destinationStatus;
            const auto inspected = inspect(destinationEntry, destinationStatus);
            if (inspected.failed())
                return inspected;
            // Do not follow links out of the source profile or accept special
            // files as successfully copied preset data.
            if (!std::filesystem::is_regular_file(status)
                || (std::filesystem::exists(destinationStatus)
                    && (!std::filesystem::is_regular_file(destinationStatus) || destinationEntry.isSymbolicLink()))
                || !sourceEntry.copyFileTo(destinationEntry))
                return juce::Result::fail("Cannot copy a FxSound preset into the evlsound profile.");
        }

        entry.increment(error);
        if (error)
            return juce::Result::fail("Cannot finish enumerating FxSound presets for import.");
    }

    return juce::Result::ok();
}
}

// Paths are supplied by the caller so this operation can be tested without
// constructing Settings or accessing any application-data or registry location.
inline juce::Result importStockProfile(const juce::File& source, const juce::File& destination)
{
    const auto validRoots = ProfileImportDetail::validateRoots(source, destination);
    if (validRoots.failed())
        return validRoots;
    const auto destinationSettings = destination.getChildFile("FxSound.settings");
    std::filesystem::file_status destinationStatus;
    const auto inspectedDestination = ProfileImportDetail::inspect(destinationSettings, destinationStatus);
    if (inspectedDestination.failed())
        return inspectedDestination;
    if (std::filesystem::exists(destinationStatus))
    {
        if (!std::filesystem::is_regular_file(destinationStatus) || destinationSettings.isSymbolicLink())
            return juce::Result::fail("The independent settings path is not a regular file.");
        return juce::Result::ok();
    }

    const auto sourceSettings = source.getChildFile("FxSound.settings");
    std::filesystem::file_status sourceStatus;
    const auto inspectedSource = ProfileImportDetail::inspect(sourceSettings, sourceStatus);
    if (inspectedSource.failed())
        return inspectedSource;
    if (std::filesystem::exists(sourceStatus))
    {
        if (!std::filesystem::is_regular_file(sourceStatus) || sourceSettings.isSymbolicLink())
            return juce::Result::fail("The stock settings path is not a regular file.");
        if (destination.createDirectory().failed())
            return juce::Result::fail("Cannot create the independent evlsound profile.");

        for (const auto* folder : { "Presets", "AutoSave" })
        {
            const auto sourceFolder = source.getChildFile(folder);
            std::filesystem::file_status status;
            const auto inspected = ProfileImportDetail::inspect(sourceFolder, status);
            if (inspected.failed())
                return inspected;
            if (sourceFolder.isSymbolicLink())
                return juce::Result::fail("A stock preset folder redirects outside its profile.");
            if (std::filesystem::is_directory(status))
            {
                const auto copied = ProfileImportDetail::copyDirectory(sourceFolder, destination.getChildFile(folder));
                if (copied.failed())
                    return copied;
            }
            else if (std::filesystem::exists(status))
                return juce::Result::fail("A FxSound preset folder is not a regular directory.");
        }

        // Copy the settings last so a partial import is retried on the next launch.
        juce::TemporaryFile pendingSettings(destinationSettings);
        if (!sourceSettings.copyFileTo(pendingSettings.getFile())
            || !pendingSettings.overwriteTargetFileWithTemporary())
            return juce::Result::fail("Cannot copy FxSound settings into the evlsound profile.");
    }

    return juce::Result::ok();
}
}
