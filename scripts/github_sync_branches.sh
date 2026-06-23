#!/usr/bin/env bash
# Sync GitHub default branch and remove legacy branch names.
# Requires: GitHub CLI (`gh`) authenticated, or GITHUB_TOKEN with repo scope.
set -euo pipefail

REPO="${GITHUB_REPO:-Huynh-ThePham/ws_vins_ros2}"
DEFAULT="${1:-main}"
LEGACY_BRANCHES=(
  baseline/euroc-verified
)

echo "Repository: ${REPO}"
echo "Target default branch: ${DEFAULT}"

if command -v gh >/dev/null 2>&1; then
  gh repo edit "${REPO}" --default-branch "${DEFAULT}"
else
  if [[ -z "${GITHUB_TOKEN:-}" ]]; then
    echo "Install GitHub CLI (gh) or set GITHUB_TOKEN, then re-run." >&2
    echo "Manual: GitHub → Settings → Branches → Default branch → ${DEFAULT}" >&2
    exit 1
  fi
  curl -fsSL -X PATCH \
    -H "Accept: application/vnd.github+json" \
    -H "Authorization: Bearer ${GITHUB_TOKEN}" \
    "https://api.github.com/repos/${REPO}" \
    -d "{\"default_branch\":\"${DEFAULT}\"}" >/dev/null
fi

echo "Default branch set to ${DEFAULT}"

for old in "${LEGACY_BRANCHES[@]}"; do
  if git ls-remote --heads origin "${old}" | grep -q .; then
    echo "Deleting legacy branch: ${old}"
    git push origin --delete "${old}" || echo "  (skip — may still be default or protected)"
  fi
done

echo "Done. Remote branches:"
git ls-remote --heads origin | sed 's|.*refs/heads/||' | sort
