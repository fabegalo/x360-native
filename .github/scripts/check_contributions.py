"""Small PR hygiene checks. No SDK, proprietary inputs or native execution."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import tomllib

import yaml


TITLE = re.compile(r"(?:feat|fix|refactor|perf|test|docs|build|ci|chore)\([a-z0-9][a-z0-9._/-]*\)!?: \S[^\r\n]*")
TEXT_SUFFIXES = {".md", ".yml", ".yaml", ".json", ".toml", ".py", ".ps1",
                 ".cpp", ".h", ".hpp", ".c", ".cmake", ".txt", ".sh", ".cmd"}
FORMS = ("bug_report", "game_compatibility", "dashboard_compatibility", "feature_request")
REQUIRED = ["CONTRIBUTING.md", "SECURITY.md", ".github/CODEOWNERS",
            ".github/PULL_REQUEST_TEMPLATE.md", ".github/ISSUE_TEMPLATE/config.yml",
            ".github/workflows/pr-checks.yml"]
REQUIRED += [f".github/ISSUE_TEMPLATE/{name}.yml" for name in FORMS]


def git(*args):
    return subprocess.check_output(["git", *args], timeout=20)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_title(title):
    require(TITLE.fullmatch(title) and title == title.strip(),
            "PR title must use type(scope): description; see CONTRIBUTING.md")


def check_form(data):
    require(isinstance(data, dict), "issue form must be a mapping")
    for key in ("name", "description"):
        require(isinstance(data.get(key), str) and data[key].strip(), f"missing {key}")
    require(isinstance(data.get("body"), list) and data["body"], "missing form body")
    ids = set()
    labels = set()
    for field in data["body"]:
        require(isinstance(field, dict), "invalid form field")
        kind, attrs = field.get("type"), field.get("attributes")
        require(isinstance(attrs, dict), "missing field attributes")
        require(kind in {"markdown", "input", "textarea", "dropdown", "checkboxes"},
                f"unsupported form field: {kind}")
        if kind == "markdown":
            require(isinstance(attrs.get("value"), str) and attrs["value"].strip(), "empty markdown")
            continue
        identifier = field.get("id", "")
        require(isinstance(identifier, str) and re.fullmatch(r"[A-Za-z0-9_-]+", identifier)
                and identifier not in ids, "missing, invalid or duplicate field id")
        ids.add(identifier)
        label = attrs.get("label")
        require(isinstance(label, str) and label.strip() and label not in labels,
                "missing or duplicate field label")
        labels.add(label)
        required = field.get("validations", {}).get("required", False)
        require(isinstance(required, bool), "required must be boolean")
        if kind in {"dropdown", "checkboxes"}:
            options = attrs.get("options")
            require(isinstance(options, list) and options, "missing options")
            if kind == "dropdown":
                require(all(isinstance(option, str) for option in options), "invalid dropdown option")
                require(len(set(options)) == len(options), "duplicate dropdown option")
    require(ids, "form needs an input field")


def check_text(path):
    text = path.read_text(encoding="utf-8")
    require(not text.startswith("\ufeff"), "remove UTF-8 BOM")
    require(not text or text.endswith("\n"), "missing final newline")
    require("\x00" not in text, "NUL byte in text file")
    require(not re.search(r"^(?:<{7}|>{7}|={7}|\|{7})(?: |$)", text, re.MULTILINE),
            "unresolved merge conflict marker")
    if path.suffix == ".json":
        json.loads(text)
    elif path.suffix == ".toml":
        tomllib.loads(text)
    elif path.suffix in {".yml", ".yaml"}:
        yaml.safe_load(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, help="base Git revision; run from repository root")
    parser.add_argument("--title", required=True)
    args = parser.parse_args()
    check_title(args.title)
    # Resolve first so option-like input cannot be interpreted as a diff flag.
    base = git("rev-parse", "--verify", "--end-of-options", f"{args.base}^{{commit}}").decode().strip()
    # Patch context lines and upstream license bytes must remain untouched.
    git("-c", "core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol",
        "diff", "--check", base, "--", ".", ":(exclude)patches/**/*.patch", ":(exclude)licenses/**")
    changed = git("diff", "--name-only", "--no-renames", "--diff-filter=ACM", "-z", base, "--")
    paths = set(filter(None, changed.decode("utf-8").split("\0"))) | set(REQUIRED)
    for filename in sorted(paths):
        path = Path(filename)
        if filename.startswith(("external/", "licenses/")) or path.suffix == ".patch":
            continue
        if path.suffix not in TEXT_SUFFIXES and filename not in REQUIRED:
            continue
        try:
            require(path.is_file() and not path.is_symlink(), "expected a regular file")
            check_text(path)
        except (ValueError, OSError, yaml.YAMLError) as exc:
            raise ValueError(f"{filename}: {exc}") from exc
    names = set()
    for name in FORMS:
        filename = f".github/ISSUE_TEMPLATE/{name}.yml"
        data = yaml.safe_load(Path(filename).read_text(encoding="utf-8"))
        try:
            check_form(data)
            require(data["name"] not in names, "duplicate form name")
            names.add(data["name"])
        except ValueError as exc:
            raise ValueError(f"{filename}: {exc}") from exc
    owners = Path(".github/CODEOWNERS").read_text(encoding="utf-8")
    require(re.search(r"^\*\s+@fabegalo\s*$", owners, re.MULTILINE), "missing default CODEOWNER")
    config = yaml.safe_load(Path(".github/ISSUE_TEMPLATE/config.yml").read_text(encoding="utf-8"))
    require(isinstance(config.get("blank_issues_enabled"), bool), "invalid issue chooser setting")
    for link in config.get("contact_links", []):
        require(all(isinstance(link.get(key), str) and link[key].strip()
                    for key in ("name", "url", "about")), "invalid issue chooser link")
    for filename in ("CONTRIBUTING.md", "SECURITY.md", ".github/PULL_REQUEST_TEMPLATE.md"):
        path = Path(filename)
        for target in re.findall(r"\]\(([^)]+)\)", path.read_text(encoding="utf-8")):
            if "://" not in target and not target.startswith("#"):
                require((path.parent / target.split("#")[0]).exists(), f"{filename}: broken link {target}")
    print("Contribution checks passed (title, changed text/metadata, forms and local links).")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError, yaml.YAMLError) as exc:
        print(f"Contribution check failed: {exc}", file=sys.stderr)
        sys.exit(1)
