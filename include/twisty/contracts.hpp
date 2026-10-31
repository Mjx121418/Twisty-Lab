#pragma once
// Wire identities, independent of kernel APIs, buffer layouts and digests.
namespace twisty::contracts {
inline constexpr const char *abstract_kind = "twisty-abstract-artifact";
inline constexpr int abstract_version = 1;
inline constexpr const char *interpretation_kind = "twisty-interpretation";
inline constexpr int interpretation_legacy_version = 1;
inline constexpr int interpretation_version = 2;
inline constexpr const char *realization_kind = "twisty-realization";
inline constexpr int realization_version = 1;
inline constexpr int backend_payload_version = 1;
inline constexpr int backend_registry_version = 1;
inline constexpr const char *controls_kind = "twisty-controls";
inline constexpr int controls_version = 1;
inline constexpr const char *presentation_kind = "twisty-presentation";
inline constexpr int presentation_version = 1;
inline constexpr int movement_presentation_version = 1;
} // namespace twisty::contracts
