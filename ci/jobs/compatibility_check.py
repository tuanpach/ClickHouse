import argparse
from pathlib import Path

from pip._vendor.packaging.version import Version

from ci.praktika.info import Info
from ci.praktika.result import Result
from ci.praktika.utils import Shell, Utils

IMAGE_UBUNTU = "clickhouse/test-old-ubuntu"
IMAGE_CENTOS = "clickhouse/test-old-centos"
DOWNLOAD_RETRIES_COUNT = 5

temp_path = Path(f"{Utils.cwd()}/ci/tmp")

# Math functions that must come from llvm-libc, not glibc.
# These are dispatched via llvmlibc_dispatch.cpp for runtime CPU detection.
# If any of these appear with @GLIBC_, it means llvm-libc dispatch is broken.
LLVMLIBC_MATH_FUNCTIONS = {
    # double
    "acos", "asin", "atan", "atan2", "cbrt", "cos", "fma", "fmod",
    "frexp", "hypot", "ldexp", "log10", "log1p", "modf", "nextafter",
    "sin", "sqrt", "tan",
    # float
    "asinf", "ldexpf", "sqrtf",
    # long double
    "ceill", "frexpl", "ldexpl", "sqrtl",
    # long/long long
    "llround", "lround",
}


def process_glibc_check():
    # See https://sourceware.org/glibc/wiki/Glibc%20Timeline
    if Utils.is_amd():
        max_glibc_version = "2.4"
    elif Utils.is_arm():
        max_glibc_version = "2.18"  # because of build with newer sysroot?
    else:
        raise RuntimeError("Can't determine max glibc version")

    commands = (
        [
            f"readelf -s --wide {temp_path}/clickhouse | grep '@GLIBC_' > {temp_path}/glibc.log",
            # FIXME: odbc bridge is not present in the deb package
            # f"readelf -s --wide {temp_path}/clickhouse-odbc-bridge | grep '@GLIBC_' >> {temp_path}/glibc.log",
            # FIXME: library bridge is not present in the deb package
            # f"readelf -s --wide {temp_path}/clickhouse-library-bridge | grep '@GLIBC_' >> {temp_path}/glibc.log",
        ],
    )

    for command in commands:
        Shell.check(command, verbose=True, strict=True)

    test_results = []
    ok = True
    with open(f"{temp_path}/glibc.log", "r", encoding="utf-8") as log:
        for line in log:
            if line.strip():
                columns = line.strip().split(" ")
                symbol_with_glibc = columns[-2]  # sysconf@GLIBC_2.2.5
                symbol_name, version = symbol_with_glibc.split("@GLIBC_")
                if version == "PRIVATE":
                    print(f"FAILED: PRIVATE version: {symbol_with_glibc}")
                    ok = False
                elif Version(version) > Version(max_glibc_version):
                    print(
                        f"FAILED: version is more than max version [{max_glibc_version}]: [{symbol_with_glibc}]"
                    )
                    ok = False
                # Check that math functions come from llvm-libc, not glibc
                if symbol_name in LLVMLIBC_MATH_FUNCTIONS:
                    print(
                        f"FAILED: math function should come from llvm-libc, not glibc: {symbol_with_glibc}"
                    )
                    ok = False
    return ok


def parse_args():
    parser = argparse.ArgumentParser("Check compatibility with old distributions")
    parser.add_argument("--check-name", required=False)
    return parser.parse_args()


def main():
    stopwatch = Utils.Stopwatch()

    check_name = Info().job_name
    assert check_name
    check_glibc = True
    # currently hardcoded to x86, don't enable for AARCH64
    check_distributions = (
        "aarch64" not in check_name.lower() and "arm" not in check_name.lower()
    )

    for package in temp_path.iterdir():
        if package.suffix == ".deb":
            Shell.check(
                f"dpkg -x {package} {temp_path} && rm {package}",
                verbose=True,
                strict=True,
            )
    Shell.check(
        f"mv {temp_path}/usr/bin/clickhouse {temp_path}/clickhouse",
        verbose=True,
        strict=True,
    )
    # Shell.check(f"chmod +x {temp_path}/clickhouse", verbose=True, strict=True)

    test_results = []

    if check_glibc:
        test_results.append(
            Result.from_commands_run(
                name="glibc version",
                command=process_glibc_check,
            )
        )

    if check_distributions:
        test_results.append(
            Result.from_commands_run(
                name="ubuntu12",
                command=[
                    f"docker run --volume={temp_path}/clickhouse:/clickhouse ubuntu:12.04 /clickhouse local --query 'select 1'",
                ],
                with_info=True,
            )
        )
        test_results.append(
            Result.from_commands_run(
                name="centos5",
                command=[
                    f"docker run --volume={temp_path}/clickhouse:/clickhouse centos:5 /clickhouse local --query 'select 1'"
                ],
                with_info=True,
            )
        )

    Result.create_from(results=test_results, stopwatch=stopwatch).complete_job()


if __name__ == "__main__":
    main()
