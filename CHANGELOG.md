# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

-   Add a library to process ADM files into `UserMetadata`.
-   Add support for ADM input in the encoder.
-   Add support for binary proto input in the encoder.

### Removed

-   Remove support for integral `deprecated_codec_id`, `deprecated_info_type`,
    `deprecated_param_definition_type`, `deprecated_loudspeaker_layout` in favor
    of enumeration-based fields.

### Changed

-   Set sensible defaults for some proto fields.

### Fixed

-   Fix parsing multi-byte UTF-8 characters on certain platforms.

## [1.0.0] - 2024-01-26

### Added

-   Add an IAMF encoder which takes in `UserMetadata` and outputs IAMF files.
-   Add a suite of `UserMetadata` textproto files to generate test IAMF files.

### Deprecated

-   Deprecate `deprecated_codec_id`, `deprecated_info_type`,
    `deprecated_param_definition_type`, `deprecated_loudspeaker_layout`.

[unreleased]: https://github.com/AOMediaCodec/iamf-tools/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/AOMediaCodec/iamf-tools/releases/tag/v1.0.0
