# Code Formatting Guide

This project uses `clang-format` to maintain consistent code style across the codebase.

## Quick Start

To format all C++ files in the project, simply run:

```bash
./format_code.sh
```

To check if files are properly formatted (without modifying them):

```bash
./format_code.sh --check
```

## Prerequisites

Install `clang-format` if you don't have it:

```bash
# Ubuntu/Debian
sudo apt-get install clang-format

# macOS
brew install clang-format
```

## Editor Integration

Most modern editors (VS Code, CLion, Vim, etc.) will automatically detect the `.clang-format` file and format your code on save. The `.editorconfig` file provides additional editor-agnostic settings.

## Configuration

The formatting rules are defined in `.clang-format` at the project root. Key settings:
- **Indentation**: 4 spaces
- **Column limit**: 120 characters
- **Brace style**: Attach (braces on same line)
- **Namespace indentation**: All content inside namespaces is indented
