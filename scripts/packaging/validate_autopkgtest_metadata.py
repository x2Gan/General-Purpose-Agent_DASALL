#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


AUTOPKGTEST_LIB = Path("/usr/share/autopkgtest/lib")


def _require_autopkgtest_lib() -> None:
    if not AUTOPKGTEST_LIB.is_dir():
        raise SystemExit(
            "[validate-autopkgtest-metadata] missing autopkgtest python library: "
            f"{AUTOPKGTEST_LIB}"
        )
    sys.path.insert(0, str(AUTOPKGTEST_LIB))


def _detect_architecture() -> str:
    result = subprocess.run(
        ["dpkg", "--print-architecture"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _all_testbed_caps(testdesc) -> list[str]:
    caps: set[str] = set()
    for required_caps in testdesc.RESTRICTIONS_REQUIRE_CAPS.values():
        for cap in required_caps:
            if isinstance(cap, str):
                caps.add(cap)
            else:
                caps.update(cap)
    return sorted(caps)


def main() -> int:
    _require_autopkgtest_lib()

    import testdesc  # type: ignore  # noqa: PLC0415

    repo_root = Path(__file__).resolve().parents[2]
    control_path = repo_root / "debian" / "tests" / "control"
    if not control_path.is_file():
        raise SystemExit(
            "[validate-autopkgtest-metadata] missing debian/tests/control"
        )

    test_arch = _detect_architecture()
    testbed_caps = _all_testbed_caps(testdesc)

    tests, some_skipped = testdesc.parse_debian_source(
        srcdir=str(repo_root),
        testbed_caps=testbed_caps,
        test_arch=test_arch,
        test_arch_is_foreign=False,
        control_path=str(control_path),
        auto_control=False,
    )

    if not tests:
        raise SystemExit(
            "[validate-autopkgtest-metadata] no tests discovered from debian/tests/control"
        )

    discovered = ", ".join(test.name for test in tests)
    print(
        "[validate-autopkgtest-metadata] validated debian/tests/control "
        f"({len(tests)} tests: {discovered})"
    )
    if some_skipped:
        print(
            "[validate-autopkgtest-metadata] note: some tests were skipped during "
            "metadata parse against synthetic full-capability testbed"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())