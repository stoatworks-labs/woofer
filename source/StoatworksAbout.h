/*
 * Stoatworks Labs - About window data for Woofer.
 *
 * A PROVISIONAL HAND COPY, adapted from fountain's on 2026-11-16, as
 * fountain's, zoetrope's and graticule's were before they were registered. Woofer is
 * not in the website's projects.json yet, so
 * stoatworks-backend/scripts/sync-about.py has nothing to generate it from.
 * Once it is registered this file is generated and must not be edited here.
 *
 * `guide` is empty because no user guide exists yet, so the About block has no
 * guide button (StoatworksAboutLinks.h leaves out a link that is not there).
 * The page and repo links point where the project will live; neither exists
 * on 2026-11-16.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Woofer";
    inline constexpr auto slug = "woofer";
    inline constexpr auto hook = "The clip printed on a loudspeaker cone that the music drives, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/woofer/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/woofer";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
