#pragma once
// Build numbers increase independently on each release channel.
namespace update_version {
#ifdef GEO_STEAM_DECK
constexpr char channel[]="deck", tag[]="v1.4.0-deck.19";
constexpr unsigned build=1040019;
#else
constexpr char channel[]="windows", tag[]="v1.4.13";
constexpr unsigned build=1041300;
#endif
}
