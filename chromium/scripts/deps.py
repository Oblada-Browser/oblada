#!/usr/bin/env python3
"""Keeps the checkout's git dependencies one commit deep when Chromium moves on.

`gclient sync --no-history` clones a new dependency with its last commit only, but brings
an existing one up to date with a plain `git fetch`: every branch, with whole histories
(moving from Chromium 155 to 156 that way took over 15 GB). sync.sh runs this around it:

  deps.py fetch <gclient root>  fetches, one commit deep, the commit each dependency needs
                                and checks it out, again for the dependencies nested in
                                others (their DEPS change with them): gclient sync then
                                finds them where it wants them, and fetches nothing
  deps.py prune <gclient root>  drops the remote branches, tags and older commits a
                                dependency was given, so it keeps its commit only, as
                                when it was cloned
"""
import pathlib
import re
import subprocess
import sys
import time

LINE = re.compile(r"^(?P<path>[^:]+): (?P<url>\S+)@(?P<sha>[0-9a-f]{40})$")


def revinfo(root):
    out = subprocess.run(["gclient", "revinfo"], cwd=root, check=True,
                         capture_output=True, text=True).stdout
    deps = []
    for line in out.splitlines():
        match = LINE.match(line.strip())
        if match and (root / match["path"] / ".git").is_dir():
            deps.append((root / match["path"], match["sha"]))
    return deps


def git(repo, *args, check=True):
    return subprocess.run(["git", "-C", str(repo), *args], check=check,
                          capture_output=True, text=True)


def fetch_one(repo, sha):
    """Fetches sha one commit deep; False if the remote doesn't give it (gclient will
    deal with it, e.g. a dependency that moved to another URL)."""
    for attempt in range(6):
        result = git(repo, "fetch", "--quiet", "--depth", "1", "--no-tags",
                     "origin", sha, check=False)
        if result.returncode == 0:
            return True
        error = result.stderr.strip()
        if "429" not in error and "RESOURCE_EXHAUSTED" not in error:
            print(f"{repo}: can't fetch {sha[:10]}: {error}", file=sys.stderr)
            return False
        # googlesource limits many fetches in a row: wait and retry.
        time.sleep(30 * (attempt + 1))
    print(f"{repo}: can't fetch {sha[:10]}: rate limited", file=sys.stderr)
    return False


def fetch(root):
    for _ in range(10):
        moved = 0
        for repo, sha in revinfo(root):
            if git(repo, "rev-parse", "HEAD").stdout.strip() == sha:
                continue
            if git(repo, "cat-file", "-e", f"{sha}^{{commit}}",
                   check=False).returncode != 0 and not fetch_one(repo, sha):
                continue
            git(repo, "checkout", "--quiet", "--detach", sha)
            print(f"{repo.relative_to(root)} -> {sha[:10]}", flush=True)
            moved += 1
        if not moved:
            return
    sys.exit("dependencies still moving after 10 rounds")


def prune(root):
    for repo, sha in revinfo(root):
        if git(repo, "rev-parse", "HEAD").stdout.strip() != sha:
            continue  # not where DEPS wants it: left alone
        shallow = repo / ".git" / "shallow"
        refs = git(repo, "for-each-ref", "--format=%(refname)", "refs/remotes",
                   "refs/tags").stdout.split()
        if not refs and shallow.is_file() and shallow.read_text().split() == [sha]:
            continue
        # One commit deep from here: what's older is no longer reachable.
        if not fetch_one(repo, sha):
            continue
        git(repo, "symbolic-ref", "--delete", "refs/remotes/origin/HEAD", check=False)
        for ref in refs:
            git(repo, "update-ref", "--no-deref", "-d", ref, check=False)
        git(repo, "reflog", "expire", "--expire=now", "--all")
        git(repo, "gc", "--prune=now", "--quiet")
        print(f"pruned {repo.relative_to(root)}", flush=True)


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("fetch", "prune"):
        sys.exit("usage: deps.py fetch|prune <gclient root>")
    {"fetch": fetch, "prune": prune}[sys.argv[1]](pathlib.Path(sys.argv[2]))
