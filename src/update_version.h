#pragma once
// Build numbers increase independently on each release channel.
namespace update_version {
#ifdef GEO_STEAM_DECK
constexpr char channel[]="deck", tag[]="v1.4.0-deck.15";
constexpr unsigned build=1040015;
#else
constexpr char channel[]="windows", tag[]="v1.4.9";
constexpr unsigned build=1040900;
#endif
}
