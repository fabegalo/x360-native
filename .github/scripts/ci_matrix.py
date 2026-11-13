"""Daily native package coverage; tags and manual runs verify all six platforms."""

import json
import os


PLATFORMS = (
    ("windows-x64", "windows-2022", "powershell", "python", "zip"),
    ("windows-arm64", "windows-11-arm", "powershell", "python", "zip"),
    ("linux-x64", "ubuntu-24.04", "pwsh", "python3", "tar.gz"),
    ("linux-arm64", "ubuntu-24.04-arm", "pwsh", "python3", "tar.gz"),
    ("macos-arm64", "macos-15", "pwsh", "python3", "zip"),
    ("macos-x64", "macos-15-intel", "pwsh", "python3", "zip"),
)
DAILY = {"windows-x64", "linux-x64", "macos-arm64"}


def package_matrix(event, ref):
    full = event == "workflow_dispatch" or ref.startswith("refs/tags/")
    return {"include": [
        dict(zip(("platform", "runner", "shell", "python", "archive"), row))
        for row in PLATFORMS if full or row[0] in DAILY
    ]}


if __name__ == "__main__":
    matrix = package_matrix(os.environ["GITHUB_EVENT_NAME"], os.environ["GITHUB_REF"])
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write("matrix=" + json.dumps(matrix, separators=(",", ":")) + "\n")
