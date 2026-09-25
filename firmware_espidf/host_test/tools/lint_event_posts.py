#!/usr/bin/env python3
"""Flag esp_event_post()/esp_event_post_to() calls that pass a non-zero timeout.

CLAUDE.md: post with a timeout of 0 and back off in the task's own vTaskDelay(),
so the task only ever takes a queue slot that is already free. A non-zero timeout
parks the caller as a waiter on the default loop's 32-slot queue, where it can be
woken ahead of the Wi-Fi task's own post and cause WIFI_EVENT_STA_DISCONNECTED to
be dropped.

Prints one tab-separated record per offending call:

    <path>\t<normalised call text>

Line numbers are deliberately omitted so the baseline survives edits elsewhere in
the file. Usage:

    lint_event_posts.py <dir>...                 # report
    lint_event_posts.py --baseline <file> <dir>. # fail only on new violations
    lint_event_posts.py --write-baseline <file> <dir>...
"""
import argparse
import os
import pathlib
import re
import sys

CALL = re.compile(r"\besp_event_post(?:_to)?\s*\(")
# Comments and string literals would confuse argument splitting.
STRIP = re.compile(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', re.S)


def split_args(text):
    """Split a balanced argument list into top-level arguments."""
    args, depth, current = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    args.append("".join(current).strip())
    return args


def find_calls(source):
    """Yield (call_text, args, is_post_to) for every post call in source."""
    blanked = STRIP.sub(lambda m: " " * len(m.group(0)), source)
    for match in CALL.finditer(blanked):
        is_post_to = "_to" in match.group(0)
        start = match.end()
        depth, i = 1, start
        while i < len(blanked) and depth:
            if blanked[i] == "(":
                depth += 1
            elif blanked[i] == ")":
                depth -= 1
            i += 1
        if depth:
            continue  # unbalanced; ignore rather than guess
        yield source[match.start():i], split_args(source[start:i - 1]), is_post_to


def scan(roots, relative_to=None):
    violations = []
    for root in roots:
        for path in sorted(pathlib.Path(root).rglob("*")):
            if path.suffix not in (".cpp", ".hpp", ".c", ".h") or not path.is_file():
                continue
            if "host_test" in path.parts or "ProtoBuf" in path.parts:
                continue
            for call, args, is_post_to in find_calls(path.read_text(errors="replace")):
                # esp_event_post takes 5 args, esp_event_post_to 6 (the loop first).
                # Anything else is a declaration, a definition or a macro.
                if len(args) != (6 if is_post_to else 5) or args[-1] == "0":
                    continue
                # A dedicated loop has its own queue, shared with nothing; the
                # shared 32-slot default queue is the one that starves Wi-Fi.
                kind = "dedicated-loop" if is_post_to else "default-loop"
                shown = path.as_posix()
                if relative_to:
                    shown = os.path.relpath(path.resolve(), relative_to)
                violations.append("%s\t%s\t%s" % (shown, kind, " ".join(call.split())))
    return sorted(violations)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("roots", nargs="+")
    ap.add_argument("--baseline")
    ap.add_argument("--write-baseline")
    ap.add_argument("--relative-to", help="emit paths relative to this directory")
    opts = ap.parse_args()

    root = pathlib.Path(opts.relative_to).resolve() if opts.relative_to else None
    found = scan(opts.roots, root)

    if opts.write_baseline:
        pathlib.Path(opts.write_baseline).write_text("\n".join(found) + "\n" if found else "")
        print("wrote %d known violations to %s" % (len(found), opts.write_baseline))
        return 0

    if not opts.baseline:
        for line in found:
            print(line)
        default_loop = sum(1 for l in found if "\tdefault-loop\t" in l)
        print(
            "\n%d call(s) with a non-zero timeout (%d onto the shared default loop)"
            % (len(found), default_loop),
            file=sys.stderr,
        )
        return 0

    known = [l for l in pathlib.Path(opts.baseline).read_text().splitlines() if l.strip()]
    new = sorted(set(found) - set(known))
    fixed = sorted(set(known) - set(found))

    for line in new:
        print("NEW      %s" % line)
    for line in fixed:
        print("FIXED    %s" % line)

    if new:
        print(
            "\n%d new esp_event_post call(s) with a non-zero timeout.\n"
            "Post with a timeout of 0 and back off in the task's own vTaskDelay()."
            % len(new),
            file=sys.stderr,
        )
        return 1
    if fixed:
        print(
            "\n%d call(s) fixed since the baseline was taken. Refresh it with:\n"
            "  make lint-baseline" % len(fixed),
            file=sys.stderr,
        )
        return 1
    print("no new esp_event_post timeout violations (%d known)" % len(known))
    return 0


if __name__ == "__main__":
    sys.exit(main())
