# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Fixed
- **Scene**: Made hierarchy bake idempotent to avoid double-resolving transforms.
- **Scene**: Preserved parent links for validation in complex hierarchies.
- **Render**: Fixed 2.5D passive camera depth sign and parallax calculations.
- **Render**: Aligned modular graph projection with direct software renderer projection.
- **Tests**: Fixed heap-buffer-overflow in adjustment layer tests (ASAN).
- **Tests**: Aligned 2.5D test expectations with new coordinate convention.
