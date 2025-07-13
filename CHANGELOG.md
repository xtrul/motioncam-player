# Changelog

## [Unreleased]
### Added
- Unit test for DNxHR HQX codec initialization

### Changed
- DNxHR export now uses the HQX profile (10-bit 4:2:2)
- Asserts enforce width multiple of 4 and even height
- Export log prints profile, pixel format, bits-per-sample and slice count
