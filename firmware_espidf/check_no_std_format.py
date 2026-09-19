# Fail the build if std::format or <format> is used. A single use links ~330 KB
# of libstdc++ (locale facets, floating_to_chars) into the nearly full OTA partition.
import os
import re

Import("env")

_pattern = re.compile(r"std::format\b|#\s*include\s*<format>")
_project_dir = env.subst("$PROJECT_DIR")
_offenders = []
for _subdir in ("src", "lib", "include"):
    for _root, _dirs, _files in os.walk(os.path.join(_project_dir, _subdir)):
        for _name in _files:
            if not _name.endswith((".c", ".cpp", ".h", ".hpp")):
                continue
            _path = os.path.join(_root, _name)
            with open(_path, encoding="utf-8", errors="ignore") as _f:
                for _lineno, _line in enumerate(_f, 1):
                    if _pattern.search(_line):
                        _offenders.append("%s:%d: %s" % (os.path.relpath(_path, _project_dir), _lineno, _line.strip()))

if _offenders:
    print("\n".join(["Error: std::format / <format> is not allowed (flash size):"] + _offenders))
    env.Exit(1)
