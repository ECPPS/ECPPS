
#!/usr/bin/env python3

from __future__ import annotations

import concurrent.futures
import os
import shutil
import subprocess
import sys

from pathlib import Path

def usage() -> None:
    print(f"Usage: {Path(sys.argv[0]).name} [report.txt]", file=sys.stderr)
    print(
        "Set CLANG_FORMAT_BUILD_DIR to select the configured build directory.",
        file=sys.stderr,
    )


def run_command(
    args: list[str],
    *,
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def find_repo_root() -> Path:
    script_path = Path(__file__).resolve()

    result = run_command(
        ["git", "-C", str(script_path.parent), "rev-parse", "--show-toplevel"]
    )

    if result.returncode != 0:
        raise RuntimeError("Could not determine Git repository root.")

    return Path(result.stdout.strip()).resolve()


def find_build_dir(repo_root: Path) -> Path:
    configured = os.environ.get("CLANG_FORMAT_BUILD_DIR")

    if configured:
        path = Path(configured)
        return (repo_root / path).resolve() if not path.is_absolute() else path

    candidates = (
        repo_root / "out/build/Linux-Clang",
        repo_root / "out/build/Win64-Clang",
    )

    for path in candidates:
        if (path / "_deps/catch2").is_dir():
            return path

    return candidates[0]


def find_translation_units(repo_root: Path) -> list[Path]:
    result = run_command(
        ["git", "-C", str(repo_root), "ls-files", "-z",
         "--", "*.cpp", "*.cxx", "*.cc", "*.ixx"]
    )

    if result.returncode != 0:
        raise RuntimeError("Could not list tracked C++ source files.")

    return [
        repo_root / filename
        for filename in result.stdout.split("\0")
        if filename
    ]


def get_jobs() -> int:
    configured = os.environ.get("CLANG_FORMAT_JOBS")

    if configured is None:
        return max(1, os.cpu_count() or 1)

    try:
        jobs = int(configured)
        if jobs < 1:
            raise ValueError
        return jobs
    except ValueError:
        raise ValueError(
            f"CLANG_FORMAT_JOBS must be a positive integer; got: {configured}"
        )


def build_command(
    source_file: Path,
    repo_root: Path,
    build_dir: Path,
) -> list[str]:

    command = [
        "clang-format",
        "-i",
        str(source_file)
    ]

    return command


def analyze_file(
    source_file: Path,
    repo_root: Path,
    build_dir: Path,
) -> tuple[str, int, str]:

    print(f"[clang-format] {source_file}", file=sys.stderr, flush=True)

    command = build_command(source_file, repo_root, build_dir)

    result = run_command(command)

    output = (
        f"File: {source_file}\n"
        f"{result.stdout}\n"
    )

    return str(source_file), result.returncode, output


def main() -> int:
    if sys.argv[1:] == ["-h"] or sys.argv[1:] == ["--help"]:
        usage()
        return 0

    if len(sys.argv) > 2:
        usage()
        return 2

    try:
        repo_root = find_repo_root()
        build_dir = find_build_dir(repo_root)
        jobs = get_jobs()

    except (RuntimeError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2

    report_file = Path(sys.argv[1] if len(sys.argv) == 2
                       else "clang-format-errors.txt")

    if not report_file.is_absolute():
        report_file = repo_root / report_file

    report_file = report_file.resolve()

    if not shutil.which("clang-format"):
        print("clang-format was not found in PATH.", file=sys.stderr)
        return 127

    try:
        report_file.parent.mkdir(parents=True, exist_ok=True)

        translation_units = find_translation_units(repo_root)

    except (OSError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1

    if not translation_units:
        print("No tracked C++ translation units found.", file=sys.stderr)
        return 2

    version = run_command(["clang-format", "--version"])

    if version.returncode != 0:
        print("Could not determine clang-format version.", file=sys.stderr)
        return 1

    print(f"Found {len(translation_units)} translation units.")
    print(f"Using {jobs} parallel workers.")

    failed = 0

    try:
        with report_file.open("w", encoding="utf-8") as report:
            report.write(
                "clang-format report\n"
                f"Version: {version.stdout.strip()}\n"
                f"Build directory: {build_dir}\n"
                f"Translation units: {len(translation_units)}\n"
                f"Parallel jobs: {jobs}\n\n"
            )

            with concurrent.futures.ProcessPoolExecutor(
                max_workers=jobs
            ) as executor:

                results = executor.map(
                    analyze_file,
                    translation_units,
                    [repo_root] * len(translation_units),
                    [build_dir] * len(translation_units),
                )

                for _, status, output in results:
                    report.write(output)

                    if status != 0:
                        failed += 1

                    report.flush()

            report.write(
                f"\nSummary: {len(translation_units)} translation units "
                f"processed; {failed} clang-format invocations returned "
                f"nonzero.\n"
                f"Full output: {report_file}\n"
            )

    except OSError as error:
        print(f"Could not write report: {error}", file=sys.stderr)
        return 1

    print(
        f"Wrote clang-format output for {len(translation_units)} "
        f"translation units to {report_file} "
        f"({failed} nonzero invocations)."
    )

    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
