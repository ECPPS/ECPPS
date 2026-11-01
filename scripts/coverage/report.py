#!/usr/bin/env python3

import argparse
import json
import subprocess
from pathlib import Path


def run(command: list[str], *, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=True, text=True, capture_output=capture)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    parser.add_argument("--llvm-cov", default="llvm-cov")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    profile_dir = build_dir / "profiles"
    output_dir = build_dir / "coverage"
    output_dir.mkdir(parents=True, exist_ok=True)

    listing = run(
        [
            "ctest",
            "--test-dir",
            str(build_dir),
            "-C",
            args.configuration,
            "--show-only=json-v1",
            "-L",
            "unit",
        ],
        capture=True,
    )
    tests = json.loads(listing.stdout)["tests"]
    objects = sorted(
        {
            Path(test["command"][0]).resolve()
            for test in tests
            if test.get("command") and Path(test["command"][0]).is_file()
        }
    )
    if not objects:
        raise SystemExit("No unit-test executables found in CTest metadata")

    profiles = sorted(profile_dir.glob("*.profraw"))
    if not profiles:
        raise SystemExit(f"No raw coverage profiles found in {profile_dir}")

    indexed_profile = output_dir / "unit.profdata"
    current_profile = output_dir / "current.profdata"
    run(
        [
            args.llvm_profdata,
            "merge",
            "-sparse",
            *map(str, profiles),
            "-o",
            str(current_profile),
        ]
    )
    if indexed_profile.exists():
        merged_profile = output_dir / "unit.merged.profdata"
        run(
            [
                args.llvm_profdata,
                "merge",
                "-sparse",
                str(indexed_profile),
                str(current_profile),
                "-o",
                str(merged_profile),
            ]
        )
        merged_profile.replace(indexed_profile)
        current_profile.unlink()
    else:
        current_profile.replace(indexed_profile)

    def llvm_objects(supplemental_objects: list[Path] | None = None) -> list[str]:
        result = [str(objects[0])]
        for binary in [*objects[1:], *(supplemental_objects or [])]:
            result.extend(["-object", str(binary)])
        return result

    ignored_paths = "(_deps/|testing/|catch_amalgamated)"
    export_command = [
        args.llvm_cov,
        "export",
        *llvm_objects(),
        f"-instr-profile={indexed_profile}",
        f"-ignore-filename-regex={ignored_paths}",
    ]
    initial_export = json.loads(run(export_command, capture=True).stdout)
    mapped_sources = {
        Path(source["filename"]).resolve()
        for data in initial_export["data"]
        for source in data["files"]
    }

    compile_commands_path = build_dir / "compile_commands.json"
    source_root = Path(__file__).resolve().parents[2]
    source_objects: dict[Path, Path] = {}
    if compile_commands_path.exists():
        for command in json.loads(compile_commands_path.read_text(encoding="utf-8")):
            source = Path(command["file"]).resolve()
            object_file = Path(command.get("output", "" )).resolve()
            if (
                object_file.suffix == ".o"
                and f"/{args.configuration}/" in object_file.as_posix()
                and object_file.is_file()
                and source.is_relative_to(source_root)
                and not source.is_relative_to(source_root / "testing")
                and not source.is_relative_to(build_dir)
            ):
                source_objects.setdefault(source, object_file)

    supplemental_objects = sorted(
        object_file
        for source, object_file in source_objects.items()
        if source not in mapped_sources
    )

    final_objects = llvm_objects(supplemental_objects)
    final_args = [
        args.llvm_cov,
        f"-instr-profile={indexed_profile}",
        f"-ignore-filename-regex={ignored_paths}",
    ]
    summary = run(
        [final_args[0], "report", *final_objects, *final_args[1:]],
        capture=True,
    ).stdout
    (output_dir / "summary.txt").write_text(summary, encoding="utf-8")

    export = run(
        [final_args[0], "export", *final_objects, *final_args[1:]],
        capture=True,
    ).stdout
    (output_dir / "coverage.json").write_text(export, encoding="utf-8")
    for profile in profiles:
        profile.unlink()
    print(summary, end="")


if __name__ == "__main__":
    main()
