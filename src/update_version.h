#pragma once
// Build numbers increase independently on each release channel.
namespace update_version {
#ifdef GEO_STEAM_DECK
constexpr char channel[]="deck", tag[]="v1.4.0-deck.11";
constexpr unsigned build=1040011;
#else
constexpr char channel[]="windows", tag[]="v1.4.5";
constexpr unsigned build=1040500;
#endif
}
