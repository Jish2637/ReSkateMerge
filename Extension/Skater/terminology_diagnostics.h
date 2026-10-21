#pragma once
// Research captures must never be enabled implicitly by the gameplay feature.
// Enable explicitly for a diagnostic build only; normal releases do no trace IO.
#ifndef DINGOSDK_TERMINOLOGY_DIAGNOSTICS
#define DINGOSDK_TERMINOLOGY_DIAGNOSTICS 0
#endif
namespace dingosdk::trick_terminology::diagnostics {
inline constexpr bool enabled=DINGOSDK_TERMINOLOGY_DIAGNOSTICS!=0;
}
