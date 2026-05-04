#!/bin/bash
set -e

# 1. Install pre-commit hooks
echo "Installing pre-commit hooks..."
pre-commit install

# 2. Install pre-push hook
echo "Installing pre-push hook..."
mkdir -p .git/hooks
cp .githooks/pre-push .git/hooks/pre-push
chmod +x .git/hooks/pre-push

echo "Quality gates installed successfully."
