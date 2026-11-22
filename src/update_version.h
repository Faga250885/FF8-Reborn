#pragma once
// Build numbers increase independently on each release channel.
namespace update_version {
#ifdef GEO_STEAM_DECK
constexpr char channel[]="deck", tag[]="v1.4.0-deck.8";
constexpr unsigned build=1040008;
#else
constexpr char channel[]="windows", tag[]="v1.4.2";
constexpr unsigned build=1040200;
#endif
}
