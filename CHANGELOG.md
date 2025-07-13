# Changelog

## Unreleased
- Switch DNxHR export to HQX (10-bit 4:2:2) profile.
- Added DnxhrExporter module with profile validation and logging.
- Basic unit test verifying HQX profile initialization.
- DNxHR export no longer restricts resolution; non-even sizes only emit a warning.
- Fixed GPU YUV conversion padding for color matrix (avoids red cast).
