# Code Formatting Guide

This project uses `clang-format` to maintain consistent code style across the codebase.

## Prerequisites

Install `clang-format`:
```bash
# Ubuntu/Debian
sudo apt-get install clang-format

# macOS
brew install clang-format

# Or use the version that comes with your compiler
```

## Formatting Code

### Format a single file
```bash
clang-format -i path/to/file.cpp
```

### Format all C++ files in the project
```bash
# From the project root
find . -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | xargs clang-format -i
```

### Format only project files (exclude concurrentqueue subdirectory)
```bash
find include src -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | xargs clang-format -i
```

### Check if files are formatted (without modifying)
```bash
find include src -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

## Editor Integration

### VS Code
Install the "C/C++" extension by Microsoft, which includes clang-format support. The `.clang-format` file will be automatically detected.

### CLion
Go to `Settings > Editor > Code Style > C/C++` and set the formatter to `clang-format`. The configuration file will be automatically detected.

### Vim/Neovim
Use plugins like `vim-clang-format` or configure your editor to run `clang-format` on save.

### Emacs
Use `clang-format` package or configure it to run on save.

## Pre-commit Hook (Optional)

To automatically format code before committing, you can set up a pre-commit hook:

1. Create `.git/hooks/pre-commit`:
```bash
#!/bin/sh
# Format staged C++ files
git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|hpp|h)$' | xargs clang-format -i
git add -u
```

2. Make it executable:
```bash
chmod +x .git/hooks/pre-commit
```

## CI/CD Integration

You can add a formatting check to your CI pipeline:

```bash
# Check if code is properly formatted
find include src -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
```

If this command fails, the code is not properly formatted.

## Configuration

The formatting rules are defined in `.clang-format` at the project root. The configuration is based on the existing code style in the project.

Key settings:
- **Indentation**: 4 spaces
- **Column limit**: 120 characters
- **Brace style**: Attach (braces on same line)
- **Pointer alignment**: Left (`int* ptr` not `int *ptr`)

